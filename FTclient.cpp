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
int ThreadNum;

long long dataLen = 0;

struct LpParam {	//RecvThreadFunc param struct
	int threadid;
	//FILE* fp;
	unsigned char* mapFileAddr;
	SOCKET ConnectSocket;
	int buflen;		//process recvbuf_len
	int recvbuf_len;//TCP recvbuf_len
	char useNagle;
	//long long offset;
	//long long recvlen;
};

DWORD WINAPI RecvThreadFunc(LPWORD lpParam) {
	SOCKET ConnectSocket = ((LpParam*)lpParam)->ConnectSocket;
	//FILE* fp = ((LpParam*)lpParam)->fp;
	unsigned char* mapFileAddr = ((LpParam*)lpParam)->mapFileAddr;
	int threadid = ((LpParam*)lpParam)->threadid;
	int recvbuf_len = ((LpParam*)lpParam)->recvbuf_len;
	int buflen = ((LpParam*)lpParam)->buflen;
	int useNagle = ((LpParam*)lpParam)->useNagle;

	int iResult;
	struct sockaddr_in name;
	int namelen = sizeof(name);
	iResult = getpeername(ConnectSocket, (sockaddr*)&name, &namelen);
	if (iResult < 0) {
		cout << "getpeername error: " << WSAGetLastError() << endl;
		closesocket(ConnectSocket);
		return -1;
	}
	cout << "thread " << threadid << " connect to " << inet_ntoa(name.sin_addr) << ":" << ntohs(name.sin_port) << endl; 

	/**
	 *modify recvbuf len
	 */
	int intlen = sizeof(recvbuf_len);
	if (setsockopt(ConnectSocket, SOL_SOCKET, SO_RCVBUF, (const char *)&recvbuf_len, intlen) < 0) {
		cout << "thread " << threadid << " setsocketopt error\n";
		closesocket(ConnectSocket);
		WSACleanup();
		return -1;
	}

	if (getsockopt(ConnectSocket, SOL_SOCKET, SO_RCVBUF, (char *)&recvbuf_len, &intlen) < 0) {
		cout << "thread " << threadid << " getsocketopt error\n";
		closesocket(ConnectSocket);
		WSACleanup();
		return -1;
	}
	cout << "thread " << threadid << " final TCP recvbuf_len = " << recvbuf_len << endl;

	/**
	 * NODELAY(don't use Nagle)
	 * Because client have no data to send, so Nagle is useless and time-consuming
	 */
	if (useNagle == 'n') {
		int on = 1, lenon = sizeof(on);
		if (setsockopt(ConnectSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, lenon) < 0) {
			cout << "thread " << threadid << " setsocketopt(NODELAY) error" << endl;
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
	//choice.3 manually assign

	clock_t start, finish;
	int count = 0;
	start = clock();
	
	//first recv offset
	long long offset;
	int longlonglen = sizeof(long long);
	iResult = recv(ConnectSocket, (char *)&offset, longlonglen, 0);
	if (iResult < 0) {
		cout << "thread " << threadid << " recv offset error: " << WSAGetLastError() << endl;
		closesocket(ConnectSocket);
		return -1;
	}

	cout << "thread " << threadid << " recv offset = " << offset << endl;
	if (threadid == ThreadNum) {
		dataLen = offset;
	}
	//_fseeki64(fp, offset, SEEK_SET);
	unsigned char* datap = mapFileAddr + offset;
	while (true) {
		//iResult = recv(ConnectSocket, recvBUF, buflen, 0);
		iResult = recv(ConnectSocket, (char*)datap, buflen, 0);
		//count ++;
		//cout << "count " << count << " : " << iResult << endl;
		if (iResult == 0) {
			cout << "thread " << threadid <<  " recv OK" << endl;
			break;
		} else if (iResult < 0) {
			cout << "thread " << threadid << " recv error: " << WSAGetLastError() << endl;
			//recallSocket(ConnectSocket);
			closesocket(ConnectSocket);
			//WSACleanup();
			return -1;
		}
		//fwrite(recvBUF, sizeof(char), iResult, fp);
		if (threadid == ThreadNum) {
			dataLen += iResult;
		}
	}
	//fclose(fp);
	finish = clock();
	cout << "thread " << threadid << " recv time: " << (double)(finish-start) << "ms" << endl;

	/**
	  *don't send, only recv
	  */
	iResult = shutdown(ConnectSocket, SD_SEND);
	if (iResult < 0) {
		cout << "thread " << threadid << " shutdown error: " << WSAGetLastError() << endl;
		//recallSocket(ConnectSocket);
		closesocket(ConnectSocket);
		//WSACleanup();
		return -1;
	}

}

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

	cout << "input TCP recvbuf_len(<= 8M): ";
	int recvbuf_len;
	//recvbuf_len = RECVBUF_LEN;
	cin >> recvbuf_len;

	/**
	 * NODELAY(don't use Nagle)
	 * Because client have no data to send, so Nagle is useless and time-consuming
	 */
	cout << "use Nagle?(y/n): " << endl;
	char useNagle;
	cin >> useNagle;

	/**
	 *recv
	 */
	//int buflen = DEFAULT_BUFLEN;		choice.1
	//int buflen = recvbuf_len;			choice.2 make process_buflen == TCP_buflen
	cout << "input process recvbuf_len: ";
	int buflen;
	cin >> buflen;						//choice.3 manually assign


	/**
	  * mapping file
	  */
	HANDLE hFile = CreateFile(
			filename,
			GENERIC_WRITE,
			FILE_SHARE_WRITE,
			NULL,       
			CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL,     
			0);    

	//返回值size_high,size_low分别表示文件大小的高32位/低32位  
	DWORD size_low,size_high;  
	long long filesize = 7000000000;	//~7G
	size_low = filesize & 0xffffffff;
	size_high = (filesize >> 32);
	cout << "low = " << size_low << " high = " << size_high << endl;

	//创建文件的内存映射文件。     
	HANDLE hMapFile=CreateFileMapping(    
			hFile,       
			NULL,     
			PAGE_READWRITE,  //对映射文件进行读写  
			size_high,      
			size_low,   //这两个参数共64位，所以支持的最大文件长度为16EB  
			NULL);
	if(hMapFile==INVALID_HANDLE_VALUE)     
	{     
		cout << "Can't create file mapping " << GetLastError() << endl;
		CloseHandle(hFile);  
		WSACleanup();
		return -1;     
	}    

	//把文件数据映射到进程的地址空间  
	void* pvFile=MapViewOfFile(  
			hMapFile,   
			FILE_MAP_WRITE,
			0,  
			0,  
			filesize);    
	unsigned char *mapFileAddr = (unsigned char*)pvFile; 
	/**
	  * multi connect, and multi recv
	  */
	cout << "thread num: ";
	cin >> ThreadNum;

	HANDLE* rThread = new HANDLE[ThreadNum];
	LpParam* lpParam = new LpParam[ThreadNum];

	clock_t begin, finish;
	for (int i = 1; i <= ThreadNum; i ++) {
		/**
		 *match a addrinfo
		 */
		for(ptr = result; ptr != NULL; ptr = ptr->ai_next)
		{
			// create a socket for client
			ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
			if(ConnectSocket == INVALID_SOCKET)
			{
				cout << "thread " << i << " client socket failed: " <<  WSAGetLastError() << endl;
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

		//freeaddrinfo(result);
		if(ConnectSocket == INVALID_SOCKET)
		{
			cout << "thread " << i << " client unable to connect to server: " << WSACleanup() << endl;
			WSACleanup();
			return -1;
		}

		/**
		  *recv
		  */
		if (i == 1) {	//begin recving
			begin = clock();
		}
		

		/*
		FILE* fp;
		if ((fp = fopen(filename, "wb")) == NULL) {
			cout << "open " << filename << " error" << endl;
			return -1;
		}
		cout << "fp = " << fp << endl;
		*/
		lpParam[i-1].threadid = i;
		//lpParam[i-1].fp = fp;
		lpParam[i-1].mapFileAddr = mapFileAddr;
		lpParam[i-1].ConnectSocket = ConnectSocket;
		lpParam[i-1].recvbuf_len = recvbuf_len;
		lpParam[i-1].buflen = buflen;
		lpParam[i-1].useNagle = useNagle;
		rThread[i-1] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RecvThreadFunc, &(lpParam[i-1]), 0, NULL);

	}

	WaitForMultipleObjects(ThreadNum, rThread, TRUE, INFINITE);
	finish = clock();
	cout << "FILE RECV TIME: " << (double)(finish-begin) << "ms" << endl;

	freeaddrinfo(result);
	//fclose(fp);
	delete [] rThread;
	delete [] lpParam;

	//recallSocket(ConnectSocket);
	//closesocket(ConnectSocket);

	/**
	  * write to file
	  */
	/*
	int chunk = 0xfffff;
	long long dataLast = dataLen;
	unsigned char* p = mapFileAddr;
	int writeNum;
	while (dataLast > chunk) {
		if (!WriteFile(hFile, p, chunk, (LPDWORD)&writeNum, NULL)) {
			cout << "file write failed" << endl;
			break;
		}
		dataLast -= writeNum;
		p += writeNum;
	}
	if (!WriteFile(hFile, p, dataLast, (LPDWORD)&writeNum, NULL)) {
		cout << "file last write failed" << endl;
	}
	*/
	UnmapViewOfFile(pvFile); //撤销映射  
    CloseHandle(hFile); //关闭文件  
	WSACleanup();
	system("PAUSE");
	return 0;
}

