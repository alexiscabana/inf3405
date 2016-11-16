#undef UNICODE

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <time.h>

// Link avec ws2_32.lib
#pragma comment(lib, "ws2_32.lib")

const int ABSTENTION = -1;

std::string receiveMessageFromServer(const SOCKET s);
int sendMessageToServer(SOCKET s, std::string str);
bool isNumber(const std::string);

using namespace std;

int __cdecl main(int argc, char **argv)
{
    WSADATA wsaData;
    SOCKET leSocket;// = INVALID_SOCKET;
    struct addrinfo *result = NULL,
                    *ptr = NULL,
                    hints;
    int iResult;
	std::vector<string> candidats;
	srand(time(0));

	#pragma region connectSocket
	//--------------------------------------------
    // InitialisATION de Winsock
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        printf("Erreur de WSAStartup: %d\n", iResult);
        return 1;
    }
	// On va creer le socket pour communiquer avec le serveur
	leSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (leSocket == INVALID_SOCKET) {
        printf("Erreur de socket(): %ld\n\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
		printf("Appuyez une touche pour finir\n");
		getchar();
        return 1;
	}
	//--------------------------------------------
	// On va chercher l'adresse du serveur en utilisant la fonction getaddrinfo.
    ZeroMemory( &hints, sizeof(hints) );
    hints.ai_family = AF_INET;        // Famille d'adresses
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;  // Protocole utilisé par le serveur

	// On indique le nom et le port du serveur auquel on veut se connecter
	string ip, p;
	cout << "Entrez l'adresse ip du serveur sous forme 127.0.0.1 : ";
	cin >> ip;
	cout << "Entrez le port du serveur (5000->5050) : ";
	cin >> p;

	char *host = new char[ip.size()];
	char* port = new char[p.size()];
	strcpy(host,ip.c_str());
	strcpy(port, p.c_str());

	// getaddrinfo obtient l'adresse IP du host donné
    iResult = getaddrinfo(host, port, &hints, &result);
    if ( iResult != 0 ) {
        printf("Erreur de getaddrinfo: %d\n", iResult);
        WSACleanup();
        return 1;
    }
	//---------------------------------------------------------------------		
	//On parcours les adresses retournees jusqu'a trouver la premiere adresse IPV4
	while((result != NULL) &&(result->ai_family!=AF_INET))   
			 result = result->ai_next; 

//	if ((result != NULL) &&(result->ai_family==AF_INET)) result = result->ai_next;  
	
	//-----------------------------------------
	if (((result == NULL) ||(result->ai_family!=AF_INET))) {
		freeaddrinfo(result);
		printf("Impossible de recuperer la bonne adresse\n\n");
        WSACleanup();
		printf("Appuyez une touche pour finir\n");
		getchar();
        return 1;
	}

	sockaddr_in *adresse;
	adresse=(struct sockaddr_in *) result->ai_addr;
	//----------------------------------------------------
	//printf("Adresse trouvee pour le serveur %s : %s\n\n", host,inet_ntoa(adresse->sin_addr));
	//printf("Tentative de connexion au serveur %s avec le port %s\n\n", inet_ntoa(adresse->sin_addr),port);
	
	// On va se connecter au serveur en utilisant l'adresse qui se trouve dans
	// la variable result.
	iResult = connect(leSocket, result->ai_addr, (int)(result->ai_addrlen));
	if (iResult == SOCKET_ERROR) {
        printf("Impossible de se connecter au serveur %s sur le port %s\n\n", inet_ntoa(adresse->sin_addr),port);
        freeaddrinfo(result);
        WSACleanup();
		printf("Appuyez une touche pour finir\n");
		getchar();
        return 1;
	}

	//printf("Connecte au serveur %s:%s\n\n", host, port);
    freeaddrinfo(result);

	#pragma endregion

	//----------------------------
	// get handshake message

	try{
		string weclcomeMess = receiveMessageFromServer(leSocket);
		printf(weclcomeMess.c_str());

		//----------------------------
		//get and show candidates
		printf("\n\nVoici les candidats : \n\n");
		int nbCand;
		recv(leSocket, (char*)&nbCand, sizeof(int) / sizeof(char), 0);
		for (int i = 1; i <= nbCand; i++){
			printf(" %d. ", i);
			string cand = receiveMessageFromServer(leSocket);
			candidats.push_back(cand);
			printf(cand.c_str());
			printf("\n\n");
		}

		//----------------------------
		//Comptabiliser vote
		bool voteCounted = false;
		do{
			printf("\n");
			cout << string(80, '_');
			printf("\n\n");
			printf("Entrez le numero de la personne pour qui vous voulez voter : ");
			//random choice (requis)
			string rep;
			//cin >> rep;
			rep = to_string(rand() % (nbCand + 1));
			printf("\n\n");

			if (!isNumber(rep)){
				printf("Vote non-comptabilise : pas un nombre");
				continue;
			}

			bool abstention = false;
			int vote = std::stoi(rep);
			if (vote > nbCand || vote < 1) {
				abstention = true;
			}

			if (abstention)
				printf("Etes-vous sur de vouloir vous abstenir (o/n)? ");
			else {
				cout << "Etes-vous sur de vouloir voter pour " << candidats[vote - 1] << " (o / n) ? ";
			}
			char confirmation = 'o';
			//cin >> confirmation;

			switch (tolower(confirmation)){
			case('o') :
			{
				int realVote = abstention ? ABSTENTION : vote - 1;
				sendMessageToServer(leSocket, to_string(realVote));
				string resp = receiveMessageFromServer(leSocket);
				cout << endl << "Message du serveur : " << resp << endl;
				voteCounted = true;
				break;
			}
			case('n') :
				continue;
			default:
				printf("Vote non-comptabilise : pas de confirmation");
				continue;
			}

		} while (!voteCounted);
	}
	catch (...) {
		cerr << "La connexion avec le serveur est expirée" << endl;
		return 1;
	}
    // cleanup
    closesocket(leSocket);
    WSACleanup();

	system("pause");
    return 0;
}


std::string receiveMessageFromServer(const SOCKET s){
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

int sendMessageToServer(SOCKET s, string str){
	//sesnd length of message to client
	int nbBytes = str.length(); //how many bytes of data
	int iResult = send(s, (char*)&nbBytes, sizeof(int) / sizeof(char), 0);
	iResult |= send(s, str.c_str(), nbBytes, 0);
	return iResult;
}

bool isNumber(const string s){
	auto it = s.begin();
	while (it != s.end() && isdigit(*it)) ++it;
	return !s.empty() && it == s.end();
}