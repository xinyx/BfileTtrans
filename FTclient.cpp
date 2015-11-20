#include <iostream>
#include <winsock2.h>
#include <WS2tcpip.h>
#include <stdlib.h>
#include <time.h>

#pragma comment (lib, "Ws2_32.lib")
using namespace std;

#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_BUFLEN (8*1024*1024)	//MAX_RECVBUF_LEN==8M
#define DEFAULT_PORT "27015"
//#define RECVBUF_LEN (1024*1024)


char recvBUF[DEFAULT_BUFLEN];

/*
int recallSocket(SOCKET socket) {
	closesocket(socket);
	WSACleanup();
	return -1;
}
*/

int main() {
	WSADATA wsadata;
	SOCKET ConnectSocket = INVALID_SOCKET;
	struct addrinfo *result = NULL, *ptr = NULL, hints;
	int iResult;

	/**
	  *init winsock
	  */
	iResult = WSAStartup(MAKEWORD(2,2), &wsadata);
	if (iResult < 0) {
		cout << "init winsock error: " << WSAGetLastError() << endl;
		return -1;
	}

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	/**
	  *analyze client address
	  */
	iResult = getaddrinfo(DEFAULT_IP, DEFAULT_PORT, &hints, &result);
	if (iResult < 0) {
		cout << "getaddrinfo error: " << WSAGetLastError() << endl;
		WSACleanup();
		return -1;
	}

	/*
	//create connect socket
	ConnectSocket =  socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (ConnectSocket < 0) {
		cout << "create connect socket error: " << WSAGetLastError() << endl;
		freeaddrinfo(result);
		WSACleanup();
		return -1;
	}

	//connect
	iResult = connect(ConnectSocket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult < 0) {
		cout << "connect error: " << iResult << endl;
		freeaddrinfo(result);
		WSACleanup();
		return -1;
	}
	freeaddrinfo(result);
	*/

	/**
	  *file to recv
	  */
	char filename[512] = {0};
	while (true) {
		cout << "name of file to recv:" ;
		cin	>> filename;
		if (filename[511] != 0) {
			cout << "file name too long, try again!" << endl;
		} else {
			break;
		}
	}

	/**
	  *match a addrinfo
	  */
	for(ptr = result; ptr != NULL; ptr = ptr->ai_next)
	{
		// create a socket for client
		ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if(ConnectSocket == INVALID_SOCKET)
		{
			printf("client socket failed with error %ld\n", WSAGetLastError());
			WSACleanup();
			return -1;
		}

		// connect to server
		iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
		if(iResult == SOCKET_ERROR)
		{
			closesocket(ConnectSocket);
			ConnectSocket = INVALID_SOCKET;// if fail try next address returned by getaddrinfo
			continue;
		}
		break;
	}

	freeaddrinfo(result);
	if(ConnectSocket == INVALID_SOCKET)
	{
		printf("client unable to connect to server\n");
		WSACleanup();
		return -1;
	}

	struct sockaddr_in name;
	int namelen = sizeof(name);
	iResult = getpeername(ConnectSocket, (sockaddr*)&name, &namelen);
	if (iResult < 0) {
		cout << "getpeername error: " << WSAGetLastError() << endl;
		return -1;
	}
	cout << "connect to " << inet_ntoa(name.sin_addr) << ":" << ntohs(name.sin_port) << endl; 

	/**
	  *modify recvbuf len
	  */
	int recvbuf_len, intlen = sizeof(recvbuf_len);
	if (getsockopt(ConnectSocket, SOL_SOCKET, SO_RCVBUF, (char *)&recvbuf_len, &intlen) < 0) {
		cout << "getsocketopt error\n";
		closesocket(ConnectSocket);
		WSACleanup();
		return -1;
	}
	cout << "initial TCP recvbuf_len = " << recvbuf_len << endl;
	cout << "input TCP recvbuf_len(<= 8M): ";
	//recvbuf_len = RECVBUF_LEN;
	cin >> recvbuf_len;
	if (setsockopt(ConnectSocket, SOL_SOCKET, SO_RCVBUF, (const char *)&recvbuf_len, intlen) < 0) {
		cout << "setsocketopt error\n";
		closesocket(ConnectSocket);
		WSACleanup();
		return -1;
	}

	if (getsockopt(ConnectSocket, SOL_SOCKET, SO_RCVBUF, (char *)&recvbuf_len, &intlen) < 0) {
		cout << "getsocketopt error\n";
		closesocket(ConnectSocket);
		WSACleanup();
		return -1;
	}
	cout << "final TCP recvbuf_len = " << recvbuf_len << endl;

	/**
	  * NODELAY(don't use Nagle)
	  * Because client have no data to send, so Nagle is useless and time-consuming
	  */
	cout << "use Nagle?(y/n): " << endl;
	char tmp;
	cin >> tmp;
	if (tmp == 'n') {
		int on = 1, lenon = sizeof(on);
		if (setsockopt(ConnectSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, lenon) < 0) {
			cout << "setsocketopt(NODELAY) error" << endl;
			closesocket(ConnectSocket);
			WSACleanup();
			return -1;
		}
	}

	

	/**
	  *recv
	  */
	//int buflen = DEFAULT_BUFLEN;		choice.1
	//int buflen = recvbuf_len;			choice.2 make process_buflen == TCP_buflen
	cout << "input process recvbuf_len: ";
	int buflen;
	cin >> buflen;						//choice.3 manually assign

	FILE* fp;
	if ((fp = fopen(filename, "wb")) == NULL) {
		cout << "open " << filename << " error" << endl;
		return -1;
	}


	clock_t start, finish;
	int count = 0;
	start = clock();
	while (true) {
		iResult = recv(ConnectSocket, recvBUF, buflen, 0);
		//count ++;
		//cout << "count " << count << " : " << iResult << endl;
		if (iResult == 0) {
			cout << "recv OK" << endl;
			break;
		} else if (iResult < 0) {
			cout << "recv error: " << WSAGetLastError() << endl;
			//recallSocket(ConnectSocket);
			closesocket(ConnectSocket);
			WSACleanup();
			return -1;
		}
		fwrite(recvBUF, sizeof(char), iResult, fp);
	}
	fclose(fp);
	finish = clock();
	cout << "recv time: " << (double)(finish-start) << "ms" << endl;

	/**
	  *don't send, only recv
	  */
	iResult = shutdown(ConnectSocket, SD_SEND);
	if (iResult < 0) {
		cout << "shutdown error: " << WSAGetLastError() << endl;
		//recallSocket(ConnectSocket);
		closesocket(ConnectSocket);
		WSACleanup();
		return -1;
	}

	//recallSocket(ConnectSocket);
	closesocket(ConnectSocket);
	WSACleanup();
	system("PAUSE");
	return 0;
}

