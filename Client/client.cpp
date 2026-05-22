#define _WINSOCK_DEPRECATED_NO_WARNINGS


#include "ChatPacket.h"
#include "NetUtil.h"

#include <winsock2.h>
#include <Windows.h>
#include <iostream>
#include <process.h>
#include <conio.h>
#include "SDL.h"

#pragma comment(lib, "ws2_32")
#pragma comment(lib, "NetCommon")


using namespace std;

char SendBuffer[1024] = { 0, };
char RecvBuffer[1024] = { 0, };

bool IsRecvThreadRunning = true;
bool IsSendThreadRunning = true;

SessionManager MySessionManager;
SOCKET MyClientID;

SDL_Event Event;

void Render(SDL_Renderer* MyRender)
{
	//system("cls");

	//for (auto Player : MySessionManager.SessionList)
	//{
	//	COORD Where;
	//	Where.X = Player.X;
	//	Where.Y = Player.Y;
	//	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), Where);
	//	cout << (char)Player.Shape << endl;
	//}

	if (!MyRender) return;

	SDL_SetRenderDrawColor(MyRender, 0, 0, 0, 255);
	SDL_RenderClear(MyRender);

	for (auto& Player : MySessionManager.SessionList)
	{
		SDL_Rect Rect;
		Rect.x = Player.X * 40;
		Rect.y = Player.Y * 40;
		Rect.w = 40;
		Rect.h = 40;

		SDL_SetRenderDrawColor(MyRender, Player.R, Player.G, Player.B, 255);
		SDL_RenderFillRect(MyRender, &Rect);
		SDL_SetRenderDrawColor(MyRender, 255, 255, 255, 255);
		SDL_RenderDrawRect(MyRender, &Rect);

	}

	SDL_RenderPresent(MyRender);
}


void ProcessPacket(SOCKET ProcessSocket, const char* InBuffer, const Header& InHeader)
{
	switch ((EPacketType)InHeader.PacketType)
	{
	case EPacketType::S2C_Login:
	{
		S2C_Login LoginPacket;
		LoginPacket.Parse(InBuffer);
		cout << LoginPacket.ToString() << endl;
		MyClientID = LoginPacket.ClientSocketID;
	}
	break;
	case EPacketType::S2C_Spawn:
	{
		S2C_Spawn SpawnData;
		SpawnData.Parse(InBuffer);
		cout << SpawnData.ToString() << endl;

		Session InSession;
		InSession.ClientSocket = SpawnData.ClientSocket;
		InSession.Shape = SpawnData.Shape;
		InSession.X = SpawnData.X;
		InSession.Y = SpawnData.Y;
		
		InSession.R = SpawnData.R;
		InSession.G = SpawnData.G;
		InSession.B = SpawnData.B;

		MySessionManager.Add(InSession);
		//Render();
	}
	break;
	case EPacketType::S2C_Move:
	{
		S2C_Move MoveData;
		MoveData.Parse(InBuffer);
		Session* FindSession = MySessionManager.GetSession(MoveData.ClientSocket);
		FindSession->X = MoveData.X;
		FindSession->Y = MoveData.Y;

		//Render();
		//std::cout << MoveData.ToString() << endl;
	}
	break;
	case EPacketType::S2C_Destroy:
	{
		S2C_Destroy DestroyPacket;
		DestroyPacket.Parse(InBuffer);

		Session* FindSession = MySessionManager.GetSession(DestroyPacket.ClientSocket);

		std::cout << "Quit : " << FindSession->ClientSocket << endl;

		MySessionManager.Delete(*FindSession);
		//Render();
	}
	break;
	}


}

unsigned WINAPI RecvThread(void* Argument)
{
	SOCKET ServerSocket = *(SOCKET*)Argument;

	while (IsRecvThreadRunning)
	{
		unsigned short PacketSize = 0;

		//header
		Header DataHeader;
		int RecvBytes = RecvAll(ServerSocket, (char*)&DataHeader, HeaderSize);
		if (RecvBytes <= 0)
		{
			cout << "header recv fail " << endl;
			break;
		}

		DataHeader.NetworkToHost();

		memset(RecvBuffer, 0, sizeof(RecvBuffer));
		//data JSON
		RecvBytes = RecvAll(ServerSocket, RecvBuffer, DataHeader.PacketSize);
		if (RecvBytes <= 0)
		{
			cout << "Data recv fail " << endl;
			break;
		}

		ProcessPacket(ServerSocket, RecvBuffer, DataHeader);
	}


	return 0;
}

unsigned WINAPI SendThread(void* Argument)
{
	//책임은 사용하는 놈이 진다.
	SOCKET ServerSocket = *(SOCKET*)Argument;

	while (IsSendThreadRunning)
	{

		int KeyCode = _getch();

		if (!(KeyCode == 'w' ||
			KeyCode == 'W' ||
			KeyCode == 'a' ||
			KeyCode == 'A' ||
			KeyCode == 's' ||
			KeyCode == 'S' ||
			KeyCode == 'd' ||
			KeyCode == 'D'))
		{
			continue;
		}

		C2S_Move MoveData;
		MoveData.ClientSocket = MyClientID;
		MoveData.Direction = KeyCode;


		//header
		Header DataHeader;
		DataHeader.MakeHeader((int)(MoveData.ToString().length()), EPacketType::C2S_Move);
		int SentBytes = SendAll(ServerSocket, (char*)&DataHeader, HeaderSize);
		if (SentBytes <= 0)
		{
			cout << "header send fail." << endl;
		}

		//Data
		SentBytes = SendAll(ServerSocket, MoveData.ToString().c_str(), (int)(MoveData.ToString().length()));
		if (SentBytes <= 0)
		{
			cout << "Data send fail." << endl;
		}
	}

	return 0;
}

int SDL_main(int argc, char* argv[])
{
	cout << "client " << endl;


	SDL_Init(SDL_INIT_EVERYTHING);
	SDL_Window* MyWindow = SDL_CreateWindow("ThreadProgramming", 100, 100, 1024, 768, SDL_WINDOW_SHOWN);
	SDL_Renderer* MyRender = SDL_CreateRenderer(MyWindow, -1, 0);

	WSAData wsaData;

	WSAStartup(MAKEWORD(2, 2), &wsaData);

	SOCKET ServerSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	SOCKADDR_IN ServerSockAddr;
	memset(&ServerSockAddr, 0, sizeof(ServerSockAddr));
	ServerSockAddr.sin_family = AF_INET;
	ServerSockAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); //192.168.0.95	127.0.0.1
	ServerSockAddr.sin_port = htons(35000);

	connect(ServerSocket, (SOCKADDR*)&ServerSockAddr, sizeof(ServerSockAddr));

	cout << "client connect" << endl;

	C2S_Login LoginData;
	LoginData.UserID = "Jihwan";
	LoginData.HashKey = "1as3f356dsd6gyhg";

	Header LoginHeader;
	LoginHeader.MakeHeader(static_cast<unsigned short>(LoginData.ToString().length()), EPacketType::C2S_Login);

	//Login 요청
	if (SendAll(ServerSocket, (char*)&LoginHeader, HeaderSize) <= 0)
	{
		cout << "login header Error" << endl;
	}

	if (SendAll(ServerSocket, LoginData.ToString().c_str(), (int)LoginData.ToString().length()) <= 0)
	{
		cout << "login data Error" << endl;
	}

	HANDLE ThreadHandles[2] = { 0, };

	//nonblocking, asynchrous
	ThreadHandles[0] = (HANDLE)_beginthreadex(0, 0, RecvThread, &ServerSocket, /*CREATE_SUSPENDED*/0, 0);
	ThreadHandles[1] = (HANDLE)_beginthreadex(0, 0, SendThread, &ServerSocket, /*CREATE_SUSPENDED*/0, 0);

	bool Running = true;
	while (Running)
	{
		SDL_PollEvent(&Event);
		// 이벤트 처리
		if (Event.type == SDL_KEYDOWN)
		{
			C2S_Move MoveData;
			MoveData.ClientSocket = MyClientID;
			if (Event.key.keysym.sym == SDLK_w)
			{
				MoveData.Direction = 'w';
			}
			if (Event.key.keysym.sym == SDLK_a)
			{
				MoveData.Direction = 'a';
			}
			if (Event.key.keysym.sym == SDLK_s)
			{
				MoveData.Direction = 's';
			}
			if (Event.key.keysym.sym == SDLK_d)
			{
				MoveData.Direction = 'd';
			}
			if (Event.key.keysym.sym == SDLK_ESCAPE)
			{
				exit(-1);
			}
			//Header
			Header DataHeader;
			DataHeader.MakeHeader((int)(MoveData.ToString().length()), EPacketType::C2S_Move);
			int SentBytes = SendAll(ServerSocket, (char*)&DataHeader, HeaderSize);
			if (SentBytes <= 0)
			{
				cout << "header send fail." << endl;
			}

			//Data
			SentBytes = SendAll(ServerSocket, MoveData.ToString().c_str(), (int)(MoveData.ToString().length()));
			if (SentBytes <= 0)
			{
				cout << "Data send fail." << endl;
			}
		}
		Render(MyRender);
	}

	//blocking
	WaitForMultipleObjects(2, ThreadHandles, FALSE, INFINITE);

	closesocket(ServerSocket);

	cout << "End Thread" << endl;

	IsSendThreadRunning = false;
	IsRecvThreadRunning = false;

	CloseHandle(ThreadHandles[0]);
	CloseHandle(ThreadHandles[1]);

	WSACleanup();


	//삭제
	SDL_DestroyWindow(MyWindow);
	SDL_DestroyRenderer(MyRender);

	SDL_Quit();

	return 0;
}