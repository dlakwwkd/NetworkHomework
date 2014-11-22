// Server.cpp : 콘솔 응용 프로그램에 대한 진입점을 정의합니다.
//

#include "stdafx.h"

#define BUF_SIZE	1024
#define LIST_SIZE	256
#define READ		1
#define WRITE		2

std::vector<SOCKET> clntSockList;
CRITICAL_SECTION hLock;

typedef struct	// socket info
{
	SOCKET hClntSock;
	SOCKADDR_IN clntAdr;
} PER_HANDLE_DATA, *LPPER_HANDLE_DATA;

typedef struct	// buffer info
{
	OVERLAPPED overlapped;
	WSABUF wsaBuf;
	char buffer[BUF_SIZE];
	int rwMode;	// READ or WRITE
	int count;
} PER_IO_DATA, *LPPER_IO_DATA;

UINT WINAPI IOCPThreadMain(LPVOID CompletionPortIO);
void ErrorHandling(char *message);

int main(int argc, char* argv[])
{
	WSADATA wsaData;
	HANDLE hComPort;
	LPPER_IO_DATA ioInfo;
	LPPER_HANDLE_DATA handleInfo;
	clntSockList.reserve(LIST_SIZE);
	InitializeCriticalSection(&hLock);

	SOCKET hServSock;
	SOCKADDR_IN servAdr;
	DWORD recvBytes, flags = 0;

	if (argc != 2)
	{
		printf("Usage : %s <port>\n", argv[0]);
		exit(1);
	}
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		ErrorHandling("WSAStartup() error!");

	hComPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	_beginthreadex(NULL, 0, IOCPThreadMain, (LPVOID)hComPort, 0, NULL);

	hServSock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (hServSock == INVALID_SOCKET)
		ErrorHandling("WSASocket() error");

	ZeroMemory(&servAdr, sizeof(servAdr));
	servAdr.sin_family = AF_INET;
	servAdr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAdr.sin_port = htons(atoi(argv[1]));

	if (bind(hServSock, (SOCKADDR*)&servAdr, sizeof(servAdr)) == SOCKET_ERROR)
		ErrorHandling("bind() error");
	if (listen(hServSock, 5) == SOCKET_ERROR)
		ErrorHandling("listen() error");

	while (true)
	{
		SOCKET hClntSock;
		SOCKADDR_IN clntAdr;
		int addrLen = sizeof(clntAdr);

		hClntSock = accept(hServSock, (SOCKADDR*)&clntAdr, &addrLen);
		if (hClntSock == INVALID_SOCKET)
			continue;
		printf("Connected client IP: %s \n", inet_ntoa(clntAdr.sin_addr));

		EnterCriticalSection(&hLock);
		clntSockList.push_back(hClntSock);
		LeaveCriticalSection(&hLock);

		handleInfo = (LPPER_HANDLE_DATA)malloc(sizeof(PER_HANDLE_DATA));
		handleInfo->hClntSock = hClntSock;
		memcpy(&(handleInfo->clntAdr), &clntAdr, addrLen);

		if(CreateIoCompletionPort((HANDLE)hClntSock, hComPort, (DWORD)handleInfo, 0) != hComPort)
			ErrorHandling("CreateIoCompletionPort() error");

		ioInfo = (LPPER_IO_DATA)malloc(sizeof(PER_IO_DATA));
		ZeroMemory(&(ioInfo->overlapped), sizeof(OVERLAPPED));
		ioInfo->wsaBuf.len = BUF_SIZE;
		ioInfo->wsaBuf.buf = ioInfo->buffer;
		ioInfo->rwMode = READ;
		ioInfo->count = 0;
		WSARecv(handleInfo->hClntSock, &(ioInfo->wsaBuf), 1, &recvBytes, &flags, &(ioInfo->overlapped), NULL);
	}

	CloseHandle(hComPort);
	closesocket(hServSock);
	WSACleanup();
	DeleteCriticalSection(&hLock);
	return 0;
}


UINT WINAPI IOCPThreadMain(LPVOID CompletionPortIO)
{
	HANDLE hComPort = (HANDLE)CompletionPortIO;
	SOCKET sock;
	DWORD bytesTrans;
	LPPER_HANDLE_DATA handleInfo;
	LPPER_IO_DATA ioInfo;
	DWORD flags = 0;

	while (true)
	{
		if (!(GetQueuedCompletionStatus(hComPort, &bytesTrans, (LPDWORD)&handleInfo, (LPOVERLAPPED*)&ioInfo, INFINITE)))
		{
			if (GetLastError() != ERROR_NETNAME_DELETED)
				ErrorHandling("GetQueuedCompletionStatus() error");
		}
		sock = handleInfo->hClntSock;

		if (ioInfo->rwMode == READ)
		{
			puts("message received!");
			if (bytesTrans == 0)	// EOF 전송 시
			{
				printf("Disconnected client IP: %s \n", inet_ntoa(handleInfo->clntAdr.sin_addr));

				EnterCriticalSection(&hLock);
				for (auto& iter = clntSockList.begin(); iter != clntSockList.end(); ++iter)
				{
					if (*iter == sock)
					{
						clntSockList.erase(iter);
						break;
					}
				}
				LeaveCriticalSection(&hLock);

				closesocket(sock);
				free(handleInfo); free(ioInfo);
				continue;
			}

			ZeroMemory(&(ioInfo->overlapped), sizeof(OVERLAPPED));
			ioInfo->wsaBuf.len = bytesTrans;
			ioInfo->rwMode = WRITE;
			ioInfo->count = 0;

			EnterCriticalSection(&hLock);
			for (auto& socket : clntSockList)
			{
				WSASend(socket, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
			}
			LeaveCriticalSection(&hLock);

			ioInfo = (LPPER_IO_DATA)malloc(sizeof(PER_IO_DATA));
			ZeroMemory(&(ioInfo->overlapped), sizeof(OVERLAPPED));
			ioInfo->wsaBuf.len = BUF_SIZE;
			ioInfo->wsaBuf.buf = ioInfo->buffer;
			ioInfo->rwMode = READ;
			ioInfo->count = 0;
			WSARecv(sock, &(ioInfo->wsaBuf), 1, NULL, &flags, &(ioInfo->overlapped), NULL);
		}
		else
		{
			puts("message sent!");

			EnterCriticalSection(&hLock);
			if (++ioInfo->count == clntSockList.size())
			{
				puts("free!");
				free(ioInfo);
			}
			LeaveCriticalSection(&hLock);
		}
	}
	return 0;
}

void ErrorHandling(char *message)
{
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}