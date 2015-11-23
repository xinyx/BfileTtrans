#include <iostream>
#include <WinSock2.h>
#include <Windows.h>
#include <WS2tcpip.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>

#pragma  comment (lib, "Ws2_32.lib")
using namespace std;

#define DEFAULT_BUFLEN (8*1024*1024)	//MAX_SNDBUF_LEN==8M
#define DEFAULT_IP NULL
#define DEFAULT_PORT "27015"
//#define SNDBUF_LEN (1024*1024)


char sendBUF[DEFAULT_BUFLEN];
int ThreadNum;

struct LpParam {	//SendThreadFunc param struct
	int threadid;
	//FILE* fp;
	unsigned char* mapFileAddr;
	SOCKET ClientSocket;
	int buflen;		//process sndbuf_len
	int sndbuf_len;	//TCP sndbuf_len
	char useNagle;
	long long offset;
	long long sendlen;
};

long long getFileSize(const char* path) {
	long long filesize = -1;      
	FILE * tmp = fopen(path, "r");
	if (tmp == NULL) {
		cout << "open error" << endl;
		return filesize;
	}
	_fseeki64(tmp, (long long)0, SEEK_END);
	filesize = _ftelli64(tmp);
	fclose(tmp);
	/*int handle = open(path, 0x0100);
	filesize = filelength(handle);
	*/
	/*
    struct stat statbuff;  
    if(stat(path, &statbuff) < 0){  
        return filesize;  
    }else{  
        filesize = statbuff.st_size;  
    } 
	*/
	/*
	HANDLE handle = CreateFile(path, FILE_READ_EA, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	if (handle != INVALID_HANDLE_VALUE)
    {
        filesize = GetFileSize(handle, NULL);
        CloseHandle(handle);
    }*/
    return filesize; 
}

DWORD WINAPI SendThreadFunc(LPWORD lpParam) {
	SOCKET ClientSocket = ((LpParam*)lpParam)->ClientSocket;
	//FILE* fp = ((LpParam*)lpParam)->fp;
	unsigned char* mapFileAddr = ((LpParam*)lpParam)->mapFileAddr;
	int threadid = ((LpParam*)lpParam)->threadid;
	int sndbuf_len = ((LpParam*)lpParam)->sndbuf_len;
	int buflen = ((LpParam*)lpParam)->buflen;
	int useNagle = ((LpParam*)lpParam)->useNagle;
	long long sendlen = ((LpParam*)lpParam)->sendlen;
	long long offset = ((LpParam*)lpParam)->offset;

	//cout << "thread " << threadid << " ClientSocket = " << (int)ClientSocket << endl;
	//cout << "thread " << sndbuf_len << endl;

	long long iResult;
	struct sockaddr_in name;
	int namelen = sizeof(name);
	iResult = getpeername(ClientSocket, (sockaddr*)&name, &namelen);
	if (iResult < 0) {
		cout << "getpeername error: " << WSAGetLastError() << endl;
		closesocket(ClientSocket);
		//WSACleanup();
		return -1;
	}
	cout << "recv a connection from " << inet_ntoa(name.sin_addr) << ":" <<  ntohs(name.sin_port) << endl; 
	
	/**
	  *modify sndbuf len
	  */
	int intlen = sizeof(sndbuf_len);
	if (setsockopt(ClientSocket, SOL_SOCKET, SO_SNDBUF, (const char *)&sndbuf_len, intlen) < 0) {
		cout << "thread " << threadid << " setsocketopt error\n";
		closesocket(ClientSocket);
		//WSACleanup();
		return -1;
	}

	if (getsockopt(ClientSocket, SOL_SOCKET, SO_SNDBUF, (char *)&sndbuf_len, &intlen) < 0) {
		cout << "thread " << threadid << " getsocketopt error: " << WSAGetLastError() << endl;
		closesocket(ClientSocket);
		//WSACleanup();
		return -1;
	}
	cout << "thread " << threadid << " final TCP sndbuf_len = " << sndbuf_len << endl;
	

	/**
	  * NODELAY(don't use Nagle)
	  * Because client have no data to send, so Nagle is useless and time-consuming
	  */
	if (useNagle == 'n') {
		int on = 1, lenon = sizeof(on);
		if (setsockopt(ClientSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, lenon) < 0) {
			cout << "thread " << threadid << " setsocketopt(NODELAY) error" << endl;
			closesocket(ClientSocket);
			return -1;

		}
	}
	/**
	  *send file
	  */
	//int buflen = DEFAULT_BUFLEN;	choice.1
	//int buflen = sndbuf_len;		choice.2 make process_buflen == TCP_buflen
	//choice.3 manually assign

	//_fseeki64(fp, offset, SEEK_SET);
	unsigned char* datap = mapFileAddr + offset;
	int len;
	clock_t start, finish;
	int count = 0;
	long long dataNotSend = sendlen;
	start = clock();
	
	//first send offset
	iResult = send(ClientSocket, (const char *)&offset, sizeof(long long), 0);
	if (iResult < 0) {
		cout << "thread " << threadid << " file send offset error: " << WSAGetLastError() << endl;
		closesocket(ClientSocket);
		return -1;
	}
	
	cout << "send offset = " << (long long)offset << endl;

	//send file body
	while (dataNotSend>=buflen){
		//len = fread(sendBUF, sizeof(char), buflen, fp);
		//cout << sendBUF << endl;
		//iResult = send(ClientSocket, sendBUF, len, 0);
		iResult = send(ClientSocket, (const char*)datap, buflen, 0);
		//count ++;
		//cout << "count " << count << " : " << iResult << endl;
		if (iResult < 0) {
			cout << "thread " << threadid << " file send error: " << WSAGetLastError() << endl;
			//recallSocket(ClientSocket);
			closesocket(ClientSocket);
			return -1;
		}

		datap += iResult;
		dataNotSend -= iResult;
	} 
	if (dataNotSend > 0) {
		//len = fread(sendBUF, sizeof(char), dataNotSend, fp);
		//iResult = send(ClientSocket, sendBUF, len, 0);
		iResult = send(ClientSocket, (const char *)datap, dataNotSend, 0);
		//count ++;
		//cout << "count " << count << " : " << iResult << endl;
		if (iResult < 0) {
			cout << "thread " << threadid << " file send error: " << WSAGetLastError() << endl;
			//recallSocket(ClientSocket);
			closesocket(ClientSocket);
			return -1;
		}	
	}
	//fclose(fp);
	finish = clock();
	cout << "thread " << threadid << " send time: " << (double)(finish-start) << "ms" << endl;

	/**
	  *don't send, only recv
	  */
	iResult = shutdown(ClientSocket, SD_SEND);
	if (iResult < 0) {
		cout << "thread " << threadid << " shutdown socket error: " << WSAGetLastError() << endl;
		//recallSocket(ClientSocket);
		closesocket(ClientSocket);
		return -1;
	}

	/**
	  *wait the ack of the last packet, make sure all of data is done.
	  */
	iResult = recv(ClientSocket, sendBUF, len, 0);
	if (iResult == 0) {
		cout << "thread " << threadid << " the file has reached completely!" << endl;
	} else if (iResult > 0) {
		cout << "thread " << threadid << " this will not occur!" << endl;
	} else {
		cout << "thread " << threadid << " the last packet has not reached!" << endl;
	}

	//close socket
	//recallSocket(ClientSocket);
	closesocket(ClientSocket);
	return 0;
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
	int iResult;
	SOCKET ListenSocket = INVALID_SOCKET;
	SOCKET ClientSocket = INVALID_SOCKET;
	struct addrinfo *result = NULL;
	struct addrinfo hints;

	/**
	 *init winsock
	 */
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

	/**
	 *analyse server address
	 */
	iResult = getaddrinfo(DEFAULT_IP, DEFAULT_PORT, &hints, &result);
	if (iResult != 0) {
		cout << "getaddrinfo error: " << WSAGetLastError() << endl;
		WSACleanup();
		return -1;
	}

	/**
	 *create listen socket
	 */
	ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (ListenSocket == INVALID_SOCKET) {
		cout << "create listen socket error: " << endl;
		freeaddrinfo(result);
		WSACleanup();
		return -1;
	}

	/**
	 *bind
	 */
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

	/**
	 *listen
	 */
	iResult = listen(ListenSocket, SOMAXCONN);
	if (iResult != 0) {
		cout << "listen error: " << WSAGetLastError() << endl;
		//recallSocket(ListenSocket);
		closesocket(ListenSocket);
		WSACleanup();
		return -1;
	}

	/**
	 *open file
	 */
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



	/**
	 *modify sndbuf len
	 */
	int sndbuf_len;//, intlen = sizeof(sndbuf_len);
	cout << "input TCP sndbuf_len(<=8M): ";
	//sndbuf_len = SNDBUF_LEN;
	cin >> sndbuf_len;

	/**
	 * NODELAY(don't use Nagle)
	 * Because client have no data to send, so Nagle is useless and time-consuming
	 */
	cout << "use Nagle?(y/n): " << endl;
	char useNagle;
	cin >> useNagle;
	/**
	 *send file
	 */
	//int buflen = DEFAULT_BUFLEN;	choice.1
	//int buflen = sndbuf_len;		choice.2 make process_buflen == TCP_buflen
	cout << "input process sndbuf_len: ";
	int buflen;
	cin >> buflen;					//choice.3 manually assign

	/**
	 * multi accept, and multi send
	 */
	cout << "thread num: ";
	cin >> ThreadNum;
	HANDLE* sThread = new HANDLE[ThreadNum];
	LpParam* lpParam = new LpParam[ThreadNum];


	/**
	 * get file size
	 */
	long long filesize = getFileSize(filename);
	cout << "filesize: " << filesize << endl;
	/**
	  * mapping file
	  */
	HANDLE hFile = CreateFile(
			filename,
			GENERIC_READ,
			FILE_SHARE_READ,
			NULL,       
			OPEN_EXISTING,  //打开已存在文件  
			FILE_ATTRIBUTE_NORMAL,     
			0);    

	//返回值size_high,size_low分别表示文件大小的高32位/低32位  
	DWORD size_low,size_high;  
	size_low = filesize & 0xffffffff;
	size_high = (filesize >> 32);
	cout << "low = " << size_low << " high = " << size_high << endl;

	//创建文件的内存映射文件。     
	HANDLE hMapFile=CreateFileMapping(    
			hFile,       
			NULL,     
			PAGE_READONLY,  //对映射文件进行读写  
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
			FILE_MAP_READ,
			0,  	//high file offset
			0,  	//low file offset
			filesize);    //Bytes to map
	cout << "size_t = : " << sizeof(SIZE_T) << endl;

	unsigned char *mapFileAddr = (unsigned char*)pvFile; 
	//cout << "map address = " << mapFileAddr << endl;

	long long blocksize = filesize / ThreadNum;
	long long lastBlockSize = blocksize + (filesize % ThreadNum);

	clock_t begin, finish;
	for (int i = 1; i <= ThreadNum; i ++) {
		ClientSocket = accept(ListenSocket, NULL, NULL);
		if (ClientSocket == INVALID_SOCKET) {
			cout << "accept error: " << WSAGetLastError() << endl;
			//recallSocket(ListenSocket);
			closesocket(ListenSocket);
			WSACleanup();
			return -1;
		}
		//cout << "accept " << (int)ClientSocket << endl;
		if (i == 1) {	//begin sending
			begin = clock();
		}

		/*
		FILE* fp;
		if ((fp = fopen(filename, "rb")) == NULL) {
			cout << "open " << filename << " error" << endl;
			return -1;
		}

		cout << "fp = " << fp << endl;
		*/
		lpParam[i-1].threadid = i;
		//lpParam[i-1].fp = fp;
		lpParam[i-1].mapFileAddr = mapFileAddr;
		lpParam[i-1].ClientSocket = ClientSocket;
		lpParam[i-1].sndbuf_len = sndbuf_len;
		lpParam[i-1].buflen = buflen;
		lpParam[i-1].useNagle = useNagle;
		lpParam[i-1].offset = (i-1)*blocksize;
		if (i == ThreadNum) 
			lpParam[i-1].sendlen = lastBlockSize;
		else
			lpParam[i-1].sendlen = blocksize;

		sThread[i-1] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)SendThreadFunc, &(lpParam[i-1]), 0, NULL);
	}

	WaitForMultipleObjects(ThreadNum, sThread, TRUE, INFINITE);

	finish = clock();
	cout << "FILE SEND TIME: " << (double)(finish-begin) << "ms" << endl;

	closesocket(ListenSocket);

	/**
	 * close thread
	 */
	for (int i = 0; i < ThreadNum; i ++) {
		CloseHandle(sThread[i]);
	}

	//recall the process resource
	//fclose(fp);
	delete [] sThread;
	delete [] lpParam;
	//closesocket(ClientSocket);
	UnmapViewOfFile(pvFile); //撤销映射  
    CloseHandle(hFile); //关闭文件  

	WSACleanup();

	system("PAUSE");
	return 0;
}
