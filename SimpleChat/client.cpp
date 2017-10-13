//
// Bachelor of Software Engineering
// Media Design School
// Auckland
// New Zealand
//
// (c) 2015 Media Design School
//
// File Name	: 
// Description	: 
// Author		: Your Name
// Mail			: your.name@mediadesign.school.nz
//

//Library Includes
#include <WS2tcpip.h>
#include <Windows.h>
#include <iostream>
#include <thread>
#include <string>
#include <chrono>

//Local Includes
#include "utils.h"
#include "consoletools.h"
#include "network.h"
#include "networkentity.h"
#include "socket.h"
#include "utils.h"

//This includes
#include "client.h"

using namespace std::chrono_literals;
using namespace std::chrono;

CClient::CClient()
	:m_recvBuffer(0)
	, m_pClientSocket(0)
	, m_connectionEstablished{ false }
	, m_heartbeatInterval{ 1000ms }
	, m_reconnecting{ false }
	, m_reconnectCount{ 0 }
	, m_maxReconnectAttempts{ 100 }
{
	ZeroMemory(&m_ServerSocketAddress, sizeof(m_ServerSocketAddress));

	//Create a Packet Array and fill it out with all zeros.
	m_recvBuffer = new char[MAX_MESSAGE_LENGTH];
	ZeroMemory(m_recvBuffer, MAX_MESSAGE_LENGTH);

}

CClient::~CClient()
{
	delete[] m_recvBuffer;
	m_recvBuffer = 0;

	delete m_pClientSocket;
	m_pClientSocket = 0;

	delete m_pWorkQueue;
	m_pWorkQueue = 0;
}

/***********************
* Initialise: Initialises a client object by creating a client socket and filling out the socket address structure with details of server to send the data to.
* @author: 
* @parameter: none
* @return: void
********************/
bool CClient::Initialise()
{
	//Local Variables to hold Server's IP address and Port NUmber as entered by the user
	char _cServerIPAddress[128];
	ZeroMemory(&_cServerIPAddress, 128);
	char _cServerPort[10];
	ZeroMemory(&_cServerPort, 10);
	unsigned short _usServerPort;

	//Local variable to hold the index of the server chosen to connect to
	char _cServerChosen[5];
	ZeroMemory(_cServerChosen, 5);
	unsigned int _uiServerIndex;

	//Local variable to hold client's name
	char userName[50];
	ZeroMemory(&m_cUserName, 50);

	//Zero out the memory for all the member variables.
	ZeroMemory(&m_cUserName, strlen(m_cUserName));

	//Create a work queue to distribute messages between the main  thread and the receive thread.
	m_pWorkQueue = new AtomicQueue<std::unique_ptr<TPacket>>;

	//Create a socket object
	m_pClientSocket = new CSocket();
	
	//Get the port number to bind the socket to
	unsigned short _usClientPort = QueryPortNumber(DEFAULT_CLIENT_PORT);
	//Initialise the socket to the port number
	if (!m_pClientSocket->Initialise(_usClientPort))
	{
		return false;
	}

	//Set the client's online status to true
	m_isOnline = true;

	//Use a boolean flag to determine if a valid server has been chosen by the client or not
	bool _bServerChosen = false;

	do {
#pragma region _GETSERVER_
		unsigned char _ucChoice = QueryOption("Do you want to broadcast for servers or manually connect (B/M)?", "BM");

		switch (_ucChoice)
		{
		case 'B':
		{
			//Question 7: Broadcast to detect server
			m_bDoBroadcast = true;
			m_pClientSocket->EnableBroadcast();
			BroadcastForServers();
			if (m_vecServerAddr.size() == 0)
			{
				std::cout << "No Servers Found " << std::endl;
				continue;
			}
			else {

				//Give a list of servers for the user to choose from :
				for (unsigned int i = 0; i < m_vecServerAddr.size(); i++)
				{
					std::cout << std::endl << "[" << i << "]" << " SERVER : found at " << ToString(m_vecServerAddr[i]) << std::endl;
				}
				std::cout << "Choose a server number to connect to :";
				gets_s(_cServerChosen);

				_uiServerIndex = atoi(_cServerChosen);
				m_ServerSocketAddress.sin_family = AF_INET;
				m_ServerSocketAddress.sin_port = m_vecServerAddr[_uiServerIndex].sin_port;
				m_ServerSocketAddress.sin_addr.S_un.S_addr = m_vecServerAddr[_uiServerIndex].sin_addr.S_un.S_addr;
				std::string _strServerAddress = ToString(m_vecServerAddr[_uiServerIndex]);
				std::cout << "Attempting to connect to server at " << _strServerAddress << std::endl;
				_bServerChosen = true;
			}
			m_bDoBroadcast = false;
			m_pClientSocket->DisableBroadcast();
			break;
		}
		case 'M':
		{
			std::cout << "Enter server IP or empty for localhost: ";

			gets_s(_cServerIPAddress);
			if (_cServerIPAddress[0] == 0)
			{
				strcpy_s(_cServerIPAddress, "127.0.0.1");
			}
			//Get the Port Number of the server
			std::cout << "Enter server's port number or empty for default server port: ";
			gets_s(_cServerPort);
			//std::cin >> _usServerPort;

			if (_cServerPort[0] == 0)
			{
				_usServerPort = DEFAULT_SERVER_PORT;
			}
			else
			{
				_usServerPort = atoi(_cServerPort);
			}
			//Fill in the details of the server's socket address structure.
			//This will be used when stamping address on outgoing packets
			m_ServerSocketAddress.sin_family = AF_INET;
			m_ServerSocketAddress.sin_port = htons(_usServerPort);
			inet_pton(AF_INET, _cServerIPAddress, &m_ServerSocketAddress.sin_addr);
			_bServerChosen = true;
			std::cout << "Attempting to connect to server at " << _cServerIPAddress << ":" << _usServerPort << std::endl;
			break;
		}
		default:
		{
			std::cout << "This is not a valid option" << std::endl;
			return false;
			break;
		}
		}
#pragma endregion _GETSERVER_

	} while (_bServerChosen == false);

	//Send a hanshake message to the server as part of the Client's Initialization process.
	//Step1: Create a handshake packet
	
	do{
		std::cout << "Please enter a username : ";
		gets_s(userName);
	} while (userName[0] == 0);

	m_username = userName;

	TPacket _packet;
	_packet.Serialize(HANDSHAKE, userName); 
	SendData(_packet.PacketData, m_ServerSocketAddress);
	return true;
}

bool CClient::BroadcastForServers()
{
	//Make a broadcast packet
	TPacket _packet;
	_packet.Serialize(BROADCAST, "Broadcast to Detect Server");

	char _pcTempBuffer[MAX_MESSAGE_LENGTH];
	//Send out a broadcast message using the broadcast address
	m_pClientSocket->SetRemoteAddress(INADDR_BROADCAST);
	m_pClientSocket->SetRemotePort(DEFAULT_SERVER_PORT);

	m_ServerSocketAddress.sin_family = AF_INET;
	m_ServerSocketAddress.sin_addr.S_un.S_addr = INADDR_BROADCAST;

	for (int i = 0; i < 10; i++) //Just try  a series of 10 ports to detect a runmning server; this is needed since we are testing multiple servers on the same local machine
	{
		m_ServerSocketAddress.sin_port = htons(DEFAULT_SERVER_PORT + i);
		SendData(_packet.PacketData, m_ServerSocketAddress);
	}
	ReceiveBroadcastMessages(_pcTempBuffer);

	return true;

}

void CClient::ReceiveBroadcastMessages(char* _pcBufferToReceiveData)
{
	//set a timer on the socket for one second
	struct timeval timeValue;
	timeValue.tv_sec = 1;
	timeValue.tv_usec = 0;
	setsockopt(m_pClientSocket->GetSocketHandle(), SOL_SOCKET, SO_RCVTIMEO,
		(char*)&timeValue, sizeof(timeValue));

	//Receive data into a local buffer
	char _buffer[MAX_MESSAGE_LENGTH];
	sockaddr_in _FromAddress;
	int iSizeOfAdd = sizeof(sockaddr_in);
	//char _pcAddress[50];

	while (m_bDoBroadcast)
	{
		// pull off the packet(s) using recvfrom()
		int _iNumOfBytesReceived = recvfrom(				// pulls a packet from a single source...
			this->m_pClientSocket->GetSocketHandle(),	// client-end socket being used to read from
			_buffer,									// incoming packet to be filled
			MAX_MESSAGE_LENGTH,							// length of incoming packet to be filled
			0,											// flags
			reinterpret_cast<sockaddr*>(&_FromAddress),	// address to be filled with packet source
			&iSizeOfAdd								// size of the above address struct.
		);

		if (_iNumOfBytesReceived < 0)
		{
			//Error in receiving data 
			int _iError = WSAGetLastError();
			//std::cout << "recvfrom failed with error " << _iError;
			if (_iError == WSAETIMEDOUT) // Socket timed out on Receive
			{
				m_bDoBroadcast = false; //Do not broadcast any more
				break;
			}
			_pcBufferToReceiveData = 0;
		}
		else if (_iNumOfBytesReceived == 0)
		{
			//The remote end has shutdown the connection
			_pcBufferToReceiveData = 0;
		}
		else
		{
			//There is valid data received.
			strcpy_s(_pcBufferToReceiveData, strlen(_buffer) + 1, _buffer);
			m_ServerSocketAddress = _FromAddress;
			m_vecServerAddr.push_back(m_ServerSocketAddress);
		}
	}//End of while loop
}

void CClient::recordHeartbeat()
{
	m_lastHeartbeatRecvd = std::chrono::high_resolution_clock::now();
}

void CClient::setHeartbeatInterval(std::chrono::milliseconds interval)
{
	m_heartbeatInterval = interval;
}

bool CClient::SendData(char* dataToSend, const sockaddr_in& address)
{
	int _iBytesToSend = (int)strlen(dataToSend) + 1;
	
	//std::cout << "Trying to send " << _pcDataToSend << " to " << _RemoteIP << ":" << ntohs(m_ServerSocketAddress.sin_port) << std::endl;
	char _message[MAX_MESSAGE_LENGTH];
	strcpy_s(_message, strlen(dataToSend) + 1, dataToSend);

	int iNumBytes = sendto(
		m_pClientSocket->GetSocketHandle(),				// socket to send through.
		dataToSend,									// data to send
		_iBytesToSend,									// number of bytes to send
		0,												// flags
		reinterpret_cast<const sockaddr*>(&address),	// address to be filled with packet target
		sizeof(address)							// size of the above address struct.
		);
	//iNumBytes;
	if (_iBytesToSend != iNumBytes)
	{
		std::cout << "There was an error in sending data from client to server" << std::endl;
		return false;
	}
	return true;
}

bool CClient::SendData(char* dataToSend)
{
	return SendData(dataToSend, m_ServerSocketAddress);
}

void CClient::ReceiveData()
{
	sockaddr_in fromAddress; // Make a local variable to extract the IP and port number of the sender from whom we are receiving
	//In this case; it should be the details of the server; since the client only ever receives from the server
	int iSizeOfAdd = sizeof(fromAddress);
	int _iNumOfBytesReceived;
	
	//For debugging purpose only, convert the Address structure to a string.
	char _pcAddress[50];
	ZeroMemory(&_pcAddress, 50);
	while(m_isOnline)
	{
		// pull off the packet(s) using recvfrom()
		_iNumOfBytesReceived = recvfrom(			// pulls a packet from a single source...
			this->m_pClientSocket->GetSocketHandle(),						// client-end socket being used to read from
			m_recvBuffer,							// incoming packet to be filled
			MAX_MESSAGE_LENGTH,					   // length of incoming packet to be filled
			0,										// flags
			reinterpret_cast<sockaddr*>(&fromAddress),	// address to be filled with packet source
			&iSizeOfAdd								// size of the above address struct.
			);
		inet_ntop(AF_INET, &fromAddress, _pcAddress, sizeof(_pcAddress));

		if (_iNumOfBytesReceived < 0)
		{
			int errorCode = WSAGetLastError();

			//Error in receiving data 
			std::unique_ptr<TPacket> packet = std::make_unique<TPacket>();
			packet->Serialize(ERROR_RECEIVING, ToString(errorCode).c_str());
			packet->FromAddress = fromAddress;
			m_pWorkQueue->push(std::move(packet));
		}
		else if (_iNumOfBytesReceived == 0)
		{
			//The remote end has shutdown the connection
			std::unique_ptr<TPacket> packet = std::make_unique<TPacket>();
			packet->Serialize(CONNECTION_CLOSE, "");
			packet->FromAddress = fromAddress;
			m_pWorkQueue->push(std::move(packet));
		}
		else
		{
			//There is valid data received.
			//Put this packet data in the workQ
			std::unique_ptr<TPacket> packet = std::make_unique<TPacket>();
			packet->Deserialize(m_recvBuffer);
			packet->FromAddress = fromAddress;
			m_pWorkQueue->push(std::move(packet));
		}
		//std::this_thread::yield(); //Yield the processor; giving the main a chance to run.
	}
}

void CClient::ProcessData(TPacket& packetRecvd)
{
	recordHeartbeat();

	switch (packetRecvd.MessageType)
	{
	case HANDSHAKE:
	{
		if (m_reconnecting) {
			m_reconnecting = false;
			m_reconnectCount = 0;
			std::cout << "connection re-established" << std::endl;
		} else {
			std::cout << "Active Users:" << std::endl;
			std::istringstream iss(packetRecvd.MessageContent);
			std::string username;
			while (iss >> username) {
				std::cout << username << std::endl;
			}
		}

		m_connectionEstablished = true;
		
		break;
	}
	case DATA:
	{
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 10);
		std::cout << "SERVER> " << packetRecvd.MessageContent << std::endl;
		break;
	}
	case ERROR_USERNAME_TAKEN:
	{
		terminateClient("That username is already taken... Now terminating");

		break;
	}
	case ERROR_RECEIVING:
	case CONNECTION_CLOSE:
	case HEARTBEAT_TIMEOUT:
	case ERROR_UNKNOWN_CLIENT:
	{
		if (m_reconnectCount < m_maxReconnectAttempts)
			attemptReconnect();
		else
			terminateClient("Server unavailable... Now terminating");

		break;
	}
	case USER_JOINED:
	{
		std::cout << "User Joined: " << packetRecvd.MessageContent << std::endl;
		break;
	}
	case USER_DISCONNECTED:
	{
		std::cout << packetRecvd.MessageContent << " disconnected." << std::endl;
		break;
	}
	case COMMAND_DISPLAY_COMMANDS:
	{
		std::cout << "Command List:" << std::endl;
		std::istringstream iss(packetRecvd.MessageContent);
		std::string commandDesc;
		while (std::getline(iss, commandDesc)) {
			std::cout << commandDesc << std::endl;
		}

		break;
	}
	case COMMAND_NOT_RECOGNIZED:
	{
		std::cout << "Unknown Command" << std::endl;

		break;
	}
	default:
		break;
	}
}

void CClient::GetRemoteIPAddress(TPacket& packet, char* sendersIP)
{
	inet_ntop(AF_INET, &(packet.FromAddress.sin_addr), sendersIP, sizeof(sendersIP));
	return;
}

void CClient::GetRemoteIPAddress(char* sendersIP)
{
	inet_ntop(AF_INET, &(m_ServerSocketAddress.sin_addr), sendersIP, sizeof(sendersIP));
	return;
}

unsigned short CClient::GetRemotePort(const TPacket& packet)
{
	return ntohs(packet.FromAddress.sin_port);
}

unsigned short CClient::GetRemotePort()
{
	return ntohs(m_ServerSocketAddress.sin_port);
}

void CClient::attemptReconnect()
{
	std::cout << "Connection lost. Attempting reconnect..." << std::endl;
	++m_reconnectCount;
	
	m_reconnecting = true;
	// Fake heartbeat to prevent heartbeat check from triggering multiple reconnect messages
	m_lastHeartbeatRecvd = high_resolution_clock::now();
	
	TPacket _packet;
	_packet.Serialize(HANDSHAKE, m_username.c_str());
	SendData(_packet.PacketData, m_ServerSocketAddress);
}

void CClient::doHeartbeat()
{
	// Sent a heartbeat at regular intervals
	auto now = high_resolution_clock::now();
	auto dHeartbeat = duration_cast<milliseconds>(now - m_lastHeartbeatSent);
	if (dHeartbeat >= m_heartbeatInterval) {
		TPacket packet;
		packet.Serialize(HEARTBEAT, "");
		SendData(packet.PacketData, m_ServerSocketAddress);

		m_lastHeartbeatSent = now;
	}
}

void CClient::terminateClient(const std::string& msg)
{
	m_isOnline = false;

	std::cout << msg;
	for (size_t i = 0; i < 3; ++i) {
		std::this_thread::sleep_for(500ms);
		std::cout << ".";
		std::this_thread::sleep_for(500ms);
	}
}

void CClient::checkHeartbeat()
{
	static bool firstRun = true;
	if (firstRun) {
		m_lastHeartbeatRecvd = high_resolution_clock::now();
		firstRun = false;
	}

	auto now = high_resolution_clock::now();
	auto timeSinceLastServerHeartbeat = duration_cast<milliseconds>(now - m_lastHeartbeatRecvd);
	if (timeSinceLastServerHeartbeat > m_heartbeatTimeout) {
		if (m_connectionEstablished) {
			std::unique_ptr<TPacket> packet = std::make_unique<TPacket>();
			packet->Serialize(HEARTBEAT_TIMEOUT, "");
			m_pWorkQueue->push(std::move(packet));
		}
		else {
			terminateClient("Connection failed... Now terminating");
		}
	}
}

void CClient::GetPacketData(char* _pcLocalBuffer)
{
	strcpy_s(_pcLocalBuffer, strlen(m_recvBuffer) + 1, m_recvBuffer);
}

AtomicQueue<std::unique_ptr<TPacket>>* CClient::GetWorkQueue()
{
	return m_pWorkQueue;
}