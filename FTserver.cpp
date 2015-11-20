#include <iostream>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <stdlib.h>
#include <time.h>

#pragma  comment (lib, "Ws2_32.lib")
using namespace std;

#define DEFAULT_BUFLEN (8*1024*1024)	//MAX_SNDBUF_LEN==8M
#define DEFAULT_IP NULL
#define DEFAULT_PORT "27015"
//#define SNDBUF_LEN (1024*1024)


char sendBUF[DEFAULT_BUFLEN];

/*
int recallSocket(SOCKET socket) {
	closesocket(socket);
	WSACleanup();
	return -1;
}
*/

int main() {
	WSADATA wsadata;
	int iResult;
	SOCKET ListenSocket = INVALID_SOCKET;
	SOCKET ClientSocket = INVALID_SOCKET;
	struct addrinfo *result = NULL;
	struct addrinfo hints;

	//init winsock
	iResult = WSAStartup(MAKEWORD(2,2), &wsadata);
	if (iResult != 0) {
		cout << "init winsock error: " << WSAGetLastError() << endl;
		return -1;
	}
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	//analyse server address
	iResult = getaddrinfo(DEFAULT_IP, DEFAULT_PORT, &hints, &result);
	if (iResult != 0) {
		cout << "getaddrinfo error: " << WSAGetLastError() << endl;
		WSACleanup();
		return -1;
	}

	//create listen socket
	ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (ListenSocket == INVALID_SOCKET) {
		cout << "create listen socket error: " << endl;
		freeaddrinfo(result);
		WSACleanup();
		return -1;
	}

	//bind
	iResult =  bind(ListenSocket, result->ai_addr, (int)(result->ai_addrlen));
	if (iResult != 0) {
		cout << "bind error: " << iResult << " " << WSAGetLastError() << endl;
		freeaddrinfo(result);
		//recallSocket(ListenSocket);
		closesocket(ListenSocket);
		WSACleanup();
		return -1;
	}
	
	freeaddrinfo(result);

	//listen
	iResult = listen(ListenSocket, SOMAXCONN);
	if (iResult != 0) {
		cout << "listen error: " << WSAGetLastError() << endl;
		//recallSocket(ListenSocket);
		closesocket(ListenSocket);
		WSACleanup();
		return -1;
	}

	//open file
	char filename[512] = {0};
	while (true) {
		cout << "file name:" ;
		cin	>> filename;
		if (filename[511] != 0) {
			cout << "file name too long, try again!" << endl;
		} else {
			break;
		}
	}

	FILE* fp;
	if ((fp = fopen(filename, "rb")) == NULL) {
		cout << "open " << filename << " error" << endl;
		return -1;
	}
	
	//accept
	ClientSocket = accept(ListenSocket, NULL, NULL);
	if (ClientSocket == INVALID_SOCKET) {
		cout << "accept error: " << WSAGetLastError() << endl;
		//recallSocket(ListenSocket);
		closesocket(ListenSocket);
		WSACleanup();
		return -1;
	}
	
	struct sockaddr_in name;
	int namelen = sizeof(name);
	iResult = getpeername(ClientSocket, (sockaddr*)&name, &namelen);
	if (iResult < 0) {
		cout << "getpeername error: " << WSAGetLastError() << endl;
		return -1;
	}
	cout << "recv a connection from " << inet_ntoa(name.sin_addr) << ":" <<  ntohs(name.sin_port) << endl; 

	//ListenSocket not needed
	closesocket(ListenSocket);

	//modify sndbuf len
	int sndbuf_len, intlen = sizeof(sndbuf_len);
	if (getsockopt(ClientSocket, SOL_SOCKET, SO_SNDBUF, (char *)&sndbuf_len, &intlen) < 0) {
		cout << "getsocketopt error\n";
		closesocket(ClientSocket);
		WSACleanup();
		return -1;
	}
	cout << "initial TCP sndbuf_len = " << sndbuf_len << endl;
	cout << "input TCP sndbuf_len(<=8M): ";
	//sndbuf_len = SNDBUF_LEN;
	cin >> sndbuf_len;
	if (setsockopt(ClientSocket, SOL_SOCKET, SO_SNDBUF, (const char *)&sndbuf_len, intlen) < 0) {
		cout << "setsocketopt error\n";
		closesocket(ClientSocket);
		WSACleanup();
		return -1;
	}

	if (getsockopt(ClientSocket, SOL_SOCKET, SO_SNDBUF, (char *)&sndbuf_len, &intlen) < 0) {
		cout << "getsocketopt error\n";
		closesocket(ClientSocket);
		WSACleanup();
		return -1;
	}
	cout << "final TCP sndbuf_len = " << sndbuf_len << endl;
	

	//send file
	//int buflen = DEFAULT_BUFLEN;	choice.1
	//int buflen = sndbuf_len;		choice.2 make process_buflen == TCP_buflen
	cout << "input process sndbuf_len: ";
	int buflen;
	cin >> buflen;					//choice.3 manually assign
	int len;
	clock_t start, finish;
	int count = 0;
	start = clock();
	do {
		len = fread(sendBUF, sizeof(char), buflen, fp);
		//cout << sendBUF << endl;
		iResult = send(ClientSocket, sendBUF, len, 0);
		//count ++;
		//cout << "count " << count << " : " << iResult << endl;
		if (iResult < 0) {
			cout << "file send error: " << WSAGetLastError() << endl;
			//recallSocket(ClientSocket);
			closesocket(ClientSocket);
			WSACleanup();
			return -1;
		}
	} while (!feof(fp));
	fclose(fp);
	finish = clock();
	cout << "send time: " << (double)(finish-start) << "ms" << endl;

	//don't send, only recv
	iResult = shutdown(ClientSocket, SD_SEND);
	if (iResult < 0) {
		cout << "shutdown socket error: " << WSAGetLastError() << endl;
		//recallSocket(ClientSocket);
		closesocket(ClientSocket);
		WSACleanup();
		return -1;
	}

	//close socket
	//recallSocket(ClientSocket);
	closesocket(ClientSocket);
	WSACleanup();

	system("PAUSE");
	return 0;
}
