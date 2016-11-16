#undef UNICODE

#include <winsock2.h>
#include <time.h>
#include <iostream>
#include <algorithm>
#include <strstream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>

using namespace std;

// link with Ws2_32.lib
#pragma comment( lib, "ws2_32.lib" )

#pragma region //ErrorEntry struct
// List of Winsock error constants mapped to an interpretation string.
// Note that this list must remain sorted by the error constants'
// values, because we do a binary search on the list when looking up
// items.
static struct ErrorEntry {
    int nID;
    const char* pcMessage;

    ErrorEntry(int id, const char* pc = 0) : 
    nID(id), 
    pcMessage(pc) 
    { 
    }

    bool operator<(const ErrorEntry& rhs) const
    {
        return nID < rhs.nID;
    }
} gaErrorList[] = {
    ErrorEntry(0,                  "No error"),
    ErrorEntry(WSAEINTR,           "Interrupted system call"),
    ErrorEntry(WSAEBADF,           "Bad file number"),
    ErrorEntry(WSAEACCES,          "Permission denied"),
    ErrorEntry(WSAEFAULT,          "Bad address"),
    ErrorEntry(WSAEINVAL,          "Invalid argument"),
    ErrorEntry(WSAEMFILE,          "Too many open sockets"),
    ErrorEntry(WSAEWOULDBLOCK,     "Operation would block"),
    ErrorEntry(WSAEINPROGRESS,     "Operation now in progress"),
    ErrorEntry(WSAEALREADY,        "Operation already in progress"),
    ErrorEntry(WSAENOTSOCK,        "Socket operation on non-socket"),
    ErrorEntry(WSAEDESTADDRREQ,    "Destination address required"),
    ErrorEntry(WSAEMSGSIZE,        "Message too long"),
    ErrorEntry(WSAEPROTOTYPE,      "Protocol wrong type for socket"),
    ErrorEntry(WSAENOPROTOOPT,     "Bad protocol option"),
    ErrorEntry(WSAEPROTONOSUPPORT, "Protocol not supported"),
    ErrorEntry(WSAESOCKTNOSUPPORT, "Socket type not supported"),
    ErrorEntry(WSAEOPNOTSUPP,      "Operation not supported on socket"),
    ErrorEntry(WSAEPFNOSUPPORT,    "Protocol family not supported"),
    ErrorEntry(WSAEAFNOSUPPORT,    "Address family not supported"),
    ErrorEntry(WSAEADDRINUSE,      "Address already in use"),
    ErrorEntry(WSAEADDRNOTAVAIL,   "Can't assign requested address"),
    ErrorEntry(WSAENETDOWN,        "Network is down"),
    ErrorEntry(WSAENETUNREACH,     "Network is unreachable"),
    ErrorEntry(WSAENETRESET,       "Net connection reset"),
    ErrorEntry(WSAECONNABORTED,    "Software caused connection abort"),
    ErrorEntry(WSAECONNRESET,      "Connection reset by peer"),
    ErrorEntry(WSAENOBUFS,         "No buffer space available"),
    ErrorEntry(WSAEISCONN,         "Socket is already connected"),
    ErrorEntry(WSAENOTCONN,        "Socket is not connected"),
    ErrorEntry(WSAESHUTDOWN,       "Can't send after socket shutdown"),
    ErrorEntry(WSAETOOMANYREFS,    "Too many references, can't splice"),
    ErrorEntry(WSAETIMEDOUT,       "Connection timed out"),
    ErrorEntry(WSAECONNREFUSED,    "Connection refused"),
    ErrorEntry(WSAELOOP,           "Too many levels of symbolic links"),
    ErrorEntry(WSAENAMETOOLONG,    "File name too long"),
    ErrorEntry(WSAEHOSTDOWN,       "Host is down"),
    ErrorEntry(WSAEHOSTUNREACH,    "No route to host"),
    ErrorEntry(WSAENOTEMPTY,       "Directory not empty"),
    ErrorEntry(WSAEPROCLIM,        "Too many processes"),
    ErrorEntry(WSAEUSERS,          "Too many users"),
    ErrorEntry(WSAEDQUOT,          "Disc quota exceeded"),
    ErrorEntry(WSAESTALE,          "Stale NFS file handle"),
    ErrorEntry(WSAEREMOTE,         "Too many levels of remote in path"),
    ErrorEntry(WSASYSNOTREADY,     "Network system is unavailable"),
    ErrorEntry(WSAVERNOTSUPPORTED, "Winsock version out of range"),
    ErrorEntry(WSANOTINITIALISED,  "WSAStartup not yet called"),
    ErrorEntry(WSAEDISCON,         "Graceful shutdown in progress"),
    ErrorEntry(WSAHOST_NOT_FOUND,  "Host not found"),
    ErrorEntry(WSANO_DATA,         "No host data of that type was found")
};
const int kNumMessages = sizeof(gaErrorList) / sizeof(ErrorEntry);
#pragma endregion

struct Candidat{
	string nom;
	int nbVotes;
};

struct Watchdog{
	std::thread* thread;
	SOCKET socket;
	sockaddr_in connection;
};

void socketConnectHandler(Watchdog& param);
void logVote(sockaddr_in& remote, sockaddr_in& me);

string welcomeMessage = "";
const string welcomeMessagePath = "WelcomeMessage.txt";
const string candidatesPath = "Candidates.txt";
const unsigned long NB_SEC_BALLOT = 1000;
volatile int nbTotalVote;
const int ABSTENTION = -1;

std::vector<Candidat> candidats;
std::ofstream logFile;
std::mutex logMut;
std::mutex votesMut;
std::atomic_bool shutoffSignal;

//// WSAGetLastErrorMessage ////////////////////////////////////////////
// A function similar in spirit to Unix's perror() that tacks a canned 
// interpretation of the value of WSAGetLastError() onto the end of a
// passed string, separated by a ": ".  Generally, you should implement
// smarter error handling than this, but for default cases and simple
// programs, this function is sufficient.
//
// This function returns a pointer to an internal static buffer, so you
// must copy the data from this function before you call it again.  It
// follows that this function is also not thread-safe.
const char* WSAGetLastErrorMessage(const char* pcMessagePrefix, int nErrorID = 0)
{
    // Build basic error string
    static char acErrorBuffer[256];
    ostrstream outs(acErrorBuffer, sizeof(acErrorBuffer));
    outs << pcMessagePrefix << ": ";

    // Tack appropriate canned message onto end of supplied message 
    // prefix. Note that we do a binary search here: gaErrorList must be
	// sorted by the error constant's value.
	ErrorEntry* pEnd = gaErrorList + kNumMessages;
    ErrorEntry Target(nErrorID ? nErrorID : WSAGetLastError());
    ErrorEntry* it = lower_bound(gaErrorList, pEnd, Target);
    if ((it != pEnd) && (it->nID == Target.nID)) {
        outs << it->pcMessage;
    }
    else {
        // Didn't find error in list, so make up a generic one
        outs << "unknown error";
    }
    outs << " (" << Target.nID << ")";

    // Finish error message off and return it.
    outs << ends;
    acErrorBuffer[sizeof(acErrorBuffer) - 1] = '\0';
    return acErrorBuffer;
}

int main(void) 
{
#pragma region openstreams
	//Buffer in the welcome message
	std::ifstream welcomeStream(welcomeMessagePath);
	if (welcomeStream.fail()){
		printf("Unable to open welcoming message\n");
		return 1;
	}
	std::stringstream buffer;
	buffer << welcomeStream.rdbuf();
	welcomeMessage = buffer.str();
	welcomeStream.close();

	//Buffer in the candidates
	std::ifstream candStream(candidatesPath);
	if (welcomeStream.fail()){
		printf("Unable to open candidates\n");
		return 1;
	}
	while (!candStream.eof()) {
		Candidat c;
		getline(candStream, c.nom);
		c.nbVotes = 0;
		candidats.push_back(c);
	}
	candStream.close();

	//open log stream
	logFile.open("elections.txt");
	logFile.seekp(ios::end);// always begin at the end

#pragma endregion
#pragma region SocketsInit
	//----------------------
	// Initialize Winsock.
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (iResult != NO_ERROR) {
		cerr << "Error at WSAStartup()\n" << endl;
		return 1;
	}

    //----------------------
    // The sockaddr_in structure specifies the address family,
    // IP address, and port for the socket that is being bound.
    
	//Recuperation de l'adresse locale
	hostent *thisHost;
	thisHost=gethostbyname("127.0.0.1");
	char* ip;
	ip=inet_ntoa(*(struct in_addr*) *thisHost->h_addr_list);
	printf("Adresse locale trouvee %s : \n\n",ip);

	//----------------------
	// Create a SOCKET family for listening 
	// incoming connection requests.

	const unsigned int minPortInc = 5000;
	const unsigned int maxPortInc = 5050;
	const int nbThread = maxPortInc - minPortInc + 1;
	shutoffSignal = false;

	Watchdog watchdogs[nbThread];// 5000 -> 5050
	char* option = "1";
	try{
		for (unsigned int i = 0; i < nbThread; i++){
			//create socket
			watchdogs[i].socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			setsockopt(watchdogs[i].socket, SOL_SOCKET, SO_REUSEADDR, option, sizeof(option));

			//create a service foreach socket
			watchdogs[i].connection.sin_family = AF_INET;
			watchdogs[i].connection.sin_addr.s_addr = inet_addr(ip);
			watchdogs[i].connection.sin_port = htons(minPortInc + i);

			//----------------------
			// Bind the socket.
			iResult = ::bind(watchdogs[i].socket, (SOCKADDR *)&watchdogs[i].connection, sizeof(watchdogs[i].connection));
			if (iResult == SOCKET_ERROR) {
				::printf("bind failed with error %u\n", WSAGetLastError());
				closesocket(watchdogs[i].socket);
				WSACleanup();
				return 1;
			}

			//bring every socket into passive listening mode
			if (listen(watchdogs[i].socket, 30) == SOCKET_ERROR) {
				closesocket(watchdogs[i].socket);
				WSACleanup();
				return 1;
			}
		}
	}
	catch (...){
		for (int i = 0; i < nbThread; i++) {
			closesocket(watchdogs[i].socket);
		}
		return 1;//terminate all threads
	}
#pragma endregion

	::printf("En attente des connections des clients ...\n\n");
    
	//spawn threads 
	for (int i = 0; i < nbThread; i++){
		watchdogs[i].thread = new std::thread(socketConnectHandler, watchdogs[i]); //start thread
	}
	time_t begin;
	time(&begin);
	std::cout << "Created Threads, starting countdown..." << endl;

	//wait for termination of ballot
	time_t timer;
	do{
		timer = time(0);
	} while (NB_SEC_BALLOT >= difftime(timer, begin));

	std::cout << "Le scrutin est termine, voici les resultats : " << endl << endl;

	int nbVoteCandidat = 0;
	for (int i = 0; i < candidats.size(); i++){
		cout << candidats[i].nom << " a " << candidats[i].nbVotes << " vote(s)." << endl;
		nbVoteCandidat += candidats[i].nbVotes;
	}

	cout << "Il y a eu " << nbTotalVote - nbVoteCandidat << " abstention(s)";

	shutoffSignal = true;

    // No longer need server sockets and watchdogs
	for (int i = 0; i < nbThread; i++) {
		watchdogs[i].thread->~thread();
		closesocket(watchdogs[i].socket);
	}

    WSACleanup();
	system("pause");
	return 0;
}

//Send a message to a socket preceded by a handshake with the client
int sendMessageToClient(const SOCKET s, const string str){
	//sesnd length of message to client
	int nbBytes = str.length(); //how many bytes of data
	int iResult = send(s, (char*)&nbBytes, sizeof(int) / sizeof(char), 0);
	iResult |= send(s, str.c_str(), nbBytes, 0);
	return iResult;
}

//Receive a message from client, preceded by a handshake
std::string receiveMessageFromClient(const SOCKET s){
	//----------------------------
	// get total length of server handshake message
	int len;
	int readBytes = recv(s, (char*)&len, sizeof(int) / sizeof(char), 0);
	char* mess = new char[len + 1];
	int rResult = recv(s, mess, len, 0);
	mess[len] = '\0'; // for null termination of string
	if (rResult <= 0)
		printf("Erreur de reception : %d\n", WSAGetLastError());
	std::string str(mess);
	free(mess);
	return str;
}

//// SocketConnectHandler ///////////////////////////////////////////////////////
// Handles the incoming data by reflecting it back to the sender.
void socketConnectHandler(Watchdog& param)
{
	for (;;) {
		if (shutoffSignal)
		{
			return; // terminate thread
		}
		sockaddr_in sinRemote;
		int nAddrSize = sizeof(sinRemote);
		// On délègue un SOCKET pour accepter la connexion entrante
		// Accept the connection, blocking call
		SOCKET sd = accept(param.socket, (sockaddr*)&sinRemote, &nAddrSize);
		if (shutoffSignal)
		{
			return; // terminate thread
		}
		if (sd != INVALID_SOCKET) {
			cerr << inet_ntoa(sinRemote.sin_addr) << ":" << ntohs(sinRemote.sin_port)
				<< " vient de se connecter sur le port "<< param.connection.sin_port << endl;
		}

		//Send Welcome message
		sendMessageToClient(sd, welcomeMessage);

		//Send list of candidates (requis fonctionnel)
		int nbCand = candidats.size();
		send(sd, (char*)&nbCand, sizeof(int) / sizeof(char), 0);
		for (auto i = candidats.begin();
			i != candidats.end(); ++i){
			sendMessageToClient(sd, (*i).nom);
		}

		int vote = std::stoi(receiveMessageFromClient(sd));// receive the vote, integer representing the index [0..size - 1];
		
		//send confirmation message (success or error)
		if (shutoffSignal) {
			sendMessageToClient(sd, "Le vote a été rejeté : le scrutin est terminé");
			return; // terminate current thread, don't log or count anything
		} else if (vote == ABSTENTION) {
			votesMut.lock();
			nbTotalVote++;
			cout << "+1 abstention" << endl;
			votesMut.unlock();
			logVote(sinRemote, param.connection);
			sendMessageToClient(sd, "Le vote compte : Abstention");
		} else if (vote >= 0 && vote < nbCand){
			votesMut.lock();
			nbTotalVote++;
			cout << "+1 vote pour " << candidats[vote].nom << " : " << ++candidats[vote].nbVotes << endl;
			votesMut.unlock();
			logVote(sinRemote, param.connection);
			sendMessageToClient(sd, "Le vote compte : vous avez voté pour " + candidats[vote].nom);
		} else {
			sendMessageToClient(sd, "Le vote a été rejeté");
		}
	}
}

void logVote(sockaddr_in& remote, sockaddr_in& me) {
	logMut.lock();
	time_t now;
	time(&now);
	struct tm* timeinfo = localtime(&now);
	//log the vote in file
	logFile << asctime(timeinfo)
		<< " : Vote from client " 
		<< inet_ntoa(remote.sin_addr) 
		<< ":" << ntohs(remote.sin_port) 
		<< " connected to port " 
		<< ntohs(me.sin_port) << endl;

	logMut.unlock();
}
