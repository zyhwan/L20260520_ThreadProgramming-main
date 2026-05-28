#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "NetUtil.h"

#include <winsock2.h>
#include <iostream>
#include "SessionManager.h"

#pragma comment(lib, "ws2_32")

#pragma comment(lib, "NetCommon")

using namespace std;

char Buffer[1024] = { 0, };

SessionManager MySessionManager;

void DisconnectSocket(SOCKET DisconnectedSocket, fd_set* Sockets)
{
	SOCKET ClosedSocket = DisconnectedSocket;

	SOCKADDR_IN ClosedSockAddr;
	memset(&ClosedSockAddr, 0, sizeof(ClosedSockAddr));
	int ClosedSockAddrLength = sizeof(ClosedSockAddr);

	getpeername(ClosedSocket, (SOCKADDR*)&ClosedSockAddr, &ClosedSockAddrLength);

	cout << "disconnect : " << inet_ntoa(ClosedSockAddr.sin_addr) << endl;

	//FD_CLR(ClosedSocket, Sockets);
	//closesocket(ClosedSocket);

	//flatbuffer로 컨버팅
	flatbuffers::FlatBufferBuilder SendBuilder;

	auto DestroyData = UserPacket::CreateS2C_Destroy(
		SendBuilder,
		(uint16_t)ClosedSocket
	);

	auto UserPacketData = UserPacket::CreatePacketData(
		SendBuilder,
		UserPacket::PacketType_S2C_Destroy,
		DestroyData.Union()
	);

	SendBuilder.Finish(UserPacketData);

	//dangling pointer
	Session* FindSession = MySessionManager.GetSession(ClosedSocket);
	MySessionManager.Delete(*FindSession);

	//모든 유저한테 이동 패킷 보내줌
	for (auto Receiver : MySessionManager.SessionList)
	{
		SendAll(Receiver.ClientSocket, SendBuilder);
	}
	FD_CLR(ClosedSocket, Sockets);
	closesocket(ClosedSocket);
}

void ProcessPacket(SOCKET ProcessSocket, const char* InBuffer)
{
	auto UserPacketData = UserPacket::GetPacketData(InBuffer);

	switch (UserPacketData->data_type())
	{
	case UserPacket::PacketType_C2S_Login:
	{
		auto LoginData = UserPacketData->data_as_C2S_Login();

		//접속 한 유저 정보 업데이트(Session)
		Session InSession;
		InSession.ClientSocket = ProcessSocket;
		InSession.UserID = LoginData->user_id()->c_str();
		InSession.X = rand() % 24 + 1; // 1 ~ 25;
		InSession.Y = rand() % 24 + 1; // 1 ~ 25;
		InSession.Shape = 65 + (rand() % 26);

		InSession.R = rand() % 255;
		InSession.G = rand() % 255;
		InSession.B = rand() % 255;

		MySessionManager.Add(InSession);
		{
			flatbuffers::FlatBufferBuilder builder;
			auto S2C_LoginData = UserPacket::CreateS2C_Login(
				builder,
				(uint16_t)ProcessSocket,          
				builder.CreateString("Welcome.")  
			);
			auto UserPacketData = UserPacket::CreatePacketData(
				builder,
				UserPacket::PacketType_S2C_Login,
				S2C_LoginData.Union()
			);

			builder.Finish(UserPacketData);
			SendAll(ProcessSocket, builder);
		}


		//접속한 모든 유저한테 현재 모든 유저의 정보를 보내준다.
		for (auto Item : MySessionManager.SessionList)
		{
			UserPacket::FVector2D pos((uint16_t)Item.X, (uint16_t)Item.Y);
			UserPacket::FColor col(Item.R, Item.G, Item.B);

			for (auto Receiver : MySessionManager.SessionList)
			{
				flatbuffers::FlatBufferBuilder builder;
				auto spawnData = UserPacket::CreateS2C_Spawn(
					builder,
					(uint16_t)Item.ClientSocket,  // clientsocket_id
					&pos,                          // position (struct 포인터)
					(int8_t)Item.Shape,            // shape
					&col                           // color (struct 포인터)
				);
				auto UserPacketData = UserPacket::CreatePacketData(
					builder,
					UserPacket::PacketType_S2C_Spawn,
					spawnData.Union()
				);

				builder.Finish(UserPacketData);
				SendAll(Receiver.ClientSocket, builder);
			}
		}
	}
	break;

	case UserPacket::PacketType_C2S_Move:
	{
		auto MoveData = UserPacketData->data_as_C2S_Move();

		Session* FindSession = MySessionManager.GetSession((SOCKET)MoveData->clientsocket_id());
		if (!FindSession) break;

		switch (MoveData->direction())
		{
		case 'W':
		case 'w':
			FindSession->Y--;
			break;
		case 'S':
		case 's':
			FindSession->Y++;
			break;
		case 'A':
		case 'a':
			FindSession->X--;
			break;
		case 'D':
		case 'd':
			FindSession->X++;
			break;
		}

		UserPacket::FVector2D newPos((uint16_t)FindSession->X, (uint16_t)FindSession->Y);

		//모든 유저한테 이동 패킷 보내줌
		for (auto Receiver : MySessionManager.SessionList)
		{
			flatbuffers::FlatBufferBuilder builder;
			auto movedata = UserPacket::CreateS2C_Move(
				builder,
				(uint16_t)FindSession->ClientSocket,  // clientsocket_id
				&newPos                                // position
			);

			auto UserPacketData = UserPacket::CreatePacketData(
				builder,
				UserPacket::PacketType_S2C_Move,
				movedata.Union()
			);

			builder.Finish(UserPacketData);
			SendAll(Receiver.ClientSocket, builder);
		}
	}
	break;
	}


}

//blocking, synchrous, multiplexing(polling)
int main()
{
	srand((unsigned int)time(nullptr));
	cout << "server start" << endl;

	WSAData wsaData;

	WSAStartup(MAKEWORD(2, 2), &wsaData);

	SOCKET ListenSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	SOCKADDR_IN ListenSockAddr;
	memset(&ListenSockAddr, 0, sizeof(ListenSockAddr));
	ListenSockAddr.sin_family = AF_INET;
	ListenSockAddr.sin_addr.s_addr = INADDR_ANY;
	ListenSockAddr.sin_port = htons(35000);

	//already use port 이미 포트 사용중
	::bind(ListenSocket, (SOCKADDR*)&ListenSockAddr, sizeof(ListenSockAddr));

	listen(ListenSocket, SOMAXCONN);



	//blocking, synchronous(TimeOut)
	TIMEVAL TimeOut;
	TimeOut.tv_sec = 0;
	TimeOut.tv_usec = 500000;

	fd_set ReadSockets;
	fd_set CopyReadSockets;

	FD_ZERO(&ReadSockets);
	FD_SET(ListenSocket, &ReadSockets);

	while (true)
	{
		CopyReadSockets = ReadSockets;

		//0.5초씩 blocking
		int ChangeCount = select(0, &CopyReadSockets, 0, 0, &TimeOut);

		if (ChangeCount <= 0)
		{
			//Server Work
			//0.5초한번 서버 작업을 하는거
			continue;
		}

		//몬가 자료 있다.
		for (int i = 0; i < (int)ReadSockets.fd_count; ++i)
		{
			if (FD_ISSET(ReadSockets.fd_array[i], &CopyReadSockets))
			{
				if (ReadSockets.fd_array[i] == ListenSocket)
				{
					//connect process
					SOCKADDR_IN ClientSockAddr;
					memset(&ClientSockAddr, 0, sizeof(ClientSockAddr));
					int ClientSockSockLength = sizeof(ClientSockAddr);

					//blocking, synchronous
					SOCKET ClientSocket = accept(ListenSocket, (SOCKADDR*)&ClientSockAddr, &ClientSockSockLength);

					cout << "connect client " << inet_ntoa(ClientSockAddr.sin_addr) << endl;

					FD_SET(ClientSocket, &ReadSockets);
				}
				else
				{
					//Data Receive

					////header
					//Header DataHeader;
					//int RecvBytes = RecvAll(ReadSockets.fd_array[i], (char*)&DataHeader, HeaderSize);
					//if (RecvBytes <= 0)
					//{
					//	cout << "header recv fail " << endl;
					//	DisconnectSocket(ReadSockets.fd_array[i], &ReadSockets);
					//	continue;
					//}

					//DataHeader.NetworkToHost();

					memset(Buffer, 0, sizeof(Buffer));
					//data JSON
					int RecvBytes = RecvAll(ReadSockets.fd_array[i], Buffer);
					if (RecvBytes <= 0)
					{
						cout << "data recv fail " << endl;
						DisconnectSocket(ReadSockets.fd_array[i], &ReadSockets);
						continue;
					}
					else
					{
						ProcessPacket(ReadSockets.fd_array[i], Buffer);
					}
				}
			}
		}
	}

	closesocket(ListenSocket);
	WSACleanup();

	return 0;
}