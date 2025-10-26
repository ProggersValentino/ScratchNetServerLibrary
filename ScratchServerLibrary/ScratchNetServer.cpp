#include "pch.h"
#include "ScratchNetServer.h"
#include "Address.h"
#include "ScratchPacketHeader.h"
#include "Payload.h"
#include "PacketSerialization.h"
#include "NetSockets.h"

std::atomic<bool> shutDownRequested = false;
const int safetyBuffer = 32;
const int packetSize = 55;
const int totalPacketSize = packetSize + safetyBuffer;

ScratchNetServer::ScratchNetServer()
{
}

void ScratchNetServer::InitializeSocket()
{
    listeningSocket = *RetrieveSocket(); //create new socket
    listeningSocket.OpenSock(30000, true);
}

void ScratchNetServer::MainProcess()
{
    //playerRecord.reserve(maxPlayers); //assign amount of players
    //playerConnected.reserve(maxPlayers); //assign amount of players

    

    char recieveBuf[totalPacketSize];
    int size = totalPacketSize;

    

    //std::thread heartBeatWorker(&ScratchNetServer::SendHeartBeat, this); //start heart beat on new thread

    while (!shutDownRequested)
    {
        Address address = *CreateAddress();
        int recievedBytes = listeningSocket.Receive(address, recieveBuf, size);

        //have recieved any packets?
        if (recievedBytes > 0)
        {
            unsigned int from_address = ntohl(address.GetAddressFromSockAddrIn());
            unsigned int from_port = ntohs(address.GetPortFromSockAddrIn());

            //find if we need to assign a new client
            ClientRecord* currentClient = nullptr; //creating a new clientRecord on the stack
            int clientConnectionStatus = DetermineClient(&address, currentClient);
            Snapshot* initialSnap = CreateEmptySnapShot();

            //how do we handle the code received
            switch (clientConnectionStatus)
            {
            case -1: //cant connect
                continue;

            case 1: //fresh connection

                currentClient->clientSSRecordKeeper->InsertNewRecord(0, *initialSnap);

                if (FindPlayer(address) != 0) //is the current player we're registering the host
                {
                    //send baseline networked objects to new client for initialization
                    InitializeNewClientWithHostState(currentClient);
                }
                

                break;
            default:
                break;
            }

            delete initialSnap; //no need for initial snap anymore

            //processing recieved packet
            ScratchPacketHeader recvHeader = *InitEmptyPacketHeader();
            Payload recievedPayload = *CreateEmptyPayload();

            printf("Recieved %d bytes\n", recievedBytes);
            printf("Received package: %s '\n'", recieveBuf);
            printf("from port: %d and ip: %d \n", from_address, from_port);

            DeconstructPacket(recieveBuf, recvHeader, recievedPayload);

            const int tBufSize = 17 + safetyBuffer;
            char tempbuf[tBufSize] = { 0 };
            int payloadLoco = sizeof(recvHeader);
            memcpy(&tempbuf, &recieveBuf[payloadLoco], tBufSize);

            if (!CompareCRC(recvHeader, tempbuf, tBufSize))
            {
                std::cout << "Failed CRC Check" << std::endl;
                continue;
            }

            std::cout << "CRC Check Succeeded" << std::endl;

            //packet maintence
            currentClient->packetAckMaintence->InsertRecievedSequenceIntoRecvBuffer(recvHeader.sequence); //insert sender's packet sequence into our local recv sequence buf

            currentClient->packetAckMaintence->OnPacketAcked(recvHeader.ack); //acknowledge the most recent packet that was recieved by the sender

            currentClient->packetAckMaintence->AcknowledgeAckbits(recvHeader.ack_bits, recvHeader.ack); //acknowledge the previous 32 packets starting from the most recent acknowledged from the sender

            if (recvHeader.sequence < packetAckMaintence->mostRecentRecievedPacket) //is the packet's sequence we just recieved higher than our most recently recieved packet sequence?
            {
                std::cout << "Packet out of order" << std::endl;
                continue;
            }

            currentClient->packetAckMaintence->mostRecentRecievedPacket = recvHeader.sequence; //only update the most recent sequence if the recieved one is higher than one the stored
            std::cout << "Packet accepted" << std::endl;


            //apply changes to other clients 

            Snapshot* extractedChanges = new Snapshot();
            Snapshot* newBaseline = new Snapshot();


            //update snapshot baseline
            switch (recvHeader.packetCode)
            {
            case 11:
                extractedChanges = DeconstructRelativePayload(recievedPayload);
                newBaseline = ApplyChangesToSnapshot(*currentClient->clientSSRecordKeeper->baselineRecord.recordedSnapshot, *extractedChanges);
                currentClient->clientSSRecordKeeper->InsertNewRecord(recvHeader.sequence, *newBaseline);

                UpdateLocalNetworkedObjectsOnClientRecords(*currentClient, *newBaseline); //update all the client record's networkedObject with this change 

                ReplicatedChangeToOtherClients(*currentClient, *newBaseline, 12); //send the change to the other connected clients

                break;

            case 12:
                extractedChanges = DeconstructAbsolutePayload(recievedPayload);
                currentClient->clientSSRecordKeeper->InsertNewRecord(recvHeader.sequence, *extractedChanges);

                UpdateLocalNetworkedObjectsOnClientRecords(*currentClient, *extractedChanges); //update all the client record's networkedObject with this change 

                ReplicatedChangeToOtherClients(*currentClient, *extractedChanges, 12); //send the change to the other connected clients
                break;
            default:
                break;
            }

            //memory management
            delete extractedChanges;
            delete newBaseline;





            //send echo
            //int sendBytes = sendto(listeningSocket.GetSocket(), recieveBuf, size, 0, (SOCKADDR*)&address.sockAddr, sizeof(address.GetSockAddrIn()));

          /*  if (sendBytes == SOCKET_ERROR)
            {
                printf("Error sending data with %d \n", WSAGetLastError());
                continue;
            }*/
        }
    }

    //heartBeatWorker.join();
    listeningSocket.Close();
}

int ScratchNetServer::FindPlayer(Address player)
{
    for (int i = 0; i < maxPlayers; i++)
    {
        if (playerConnected[i] && *playerRecord[i]->clientAddress == player)
        {
            return i;
        }
    }

    return -1;
}

int ScratchNetServer::FindFreeClientIndex() const
{
    for (int i = 0; i < maxPlayers; i++)
    {
        if (!playerConnected[i]) //if there is a free spot the playerconnected array will have a spot that is set to false
        {
            return i;
        }
    }
    return -1;
}

bool ScratchNetServer::IsClientConnected(int clientIndex)
{
    return playerConnected[clientIndex];
}

const Address& ScratchNetServer::GetClientAddress(int clientIndex)
{
    return *playerRecord[clientIndex]->clientAddress;
}

ClientRecord* ScratchNetServer::GetClientRecord(int clientIndex)
{
    return playerRecord[clientIndex];
}

bool ScratchNetServer::TryToAddPlayer(Address* potentialPlayer, ClientRecord*& OUTRecord)
{
    int playerLoco = FindPlayer(*potentialPlayer);

    if (playerLoco > 0 && IsClientConnected(playerLoco)) //player is already in 
    {
        std::cout << "ERROR: Player Already Connected! \n" << std::endl;
        return false;
    }

    int freeSpot = FindFreeClientIndex();

    if (freeSpot < 0) //lobby full 
    {
        std::cout << "ERROR: Lobby is full! \n" << std::endl;
        return false;
    }

    playerRecord[freeSpot] = new ClientRecord(*potentialPlayer);
    playerConnected[freeSpot] = true;

    if (OUTRecord == nullptr)
    {
        OUTRecord = playerRecord[freeSpot];
    }

    return true;
}

int ScratchNetServer::DetermineClient(Address* clientAddress, ClientRecord*& OUTRecord)
{
    ClientRecord newClient = ClientRecord(); //creating a new clientRecord on the stack

    int index = FindPlayer(*clientAddress);

    if (index == -1)
    {
        bool playerAssigned = TryToAddPlayer(clientAddress, OUTRecord);

        if (playerAssigned) //first time connection
        {
            std::cout << "New player assigned and connected :)" << std::endl;
            return 1;
        }
        else //do not process what this client is trying to send 
        {
            std::cout << "Failed to connect new player :(" << std::endl;
            return -1;
        }


    }
    else //long time connected
    {
        OUTRecord = playerRecord[index];
        return 0;
    }

}

bool ScratchNetServer::UpdateClientObjects(ClientRecord* clientToUpdate, Snapshot selectedObjectToUpdate)
{
    if (clientToUpdate == nullptr)
    {
        return false;
    }


    if (!clientToUpdate->TryUpdatingNetworkedObject(selectedObjectToUpdate.objectId, selectedObjectToUpdate))
    {
        clientToUpdate->TryInsertNewNetworkObject(selectedObjectToUpdate.objectId, selectedObjectToUpdate);
    }

    return true;
}

void ScratchNetServer::UpdateLocalNetworkedObjectsOnClientRecords(ClientRecord clientWithUpdates, Snapshot ObjectChanges)
{
    for (int i = 0; i < playerConnected.size(); i++)
    {

        ClientRecord* client = GetClientRecord(i);

        UpdateClientObjects(client, ObjectChanges);


    }
}

void ScratchNetServer::ReplicateChangeGroupToAllClients()
{

    for (int i = 0; i < playerConnected.size(); i++)
    {
        if (!playerConnected[i]) //dont need to update a disconnected player
        {
            continue;
        }

        char transmitBuf[totalPacketSize] = { 0 };

        ClientRecord client = *GetClientRecord(i);

        for (auto pair = client.networkedObjects.begin(); pair != client.networkedObjects.end(); ++pair)
        {
            ScratchPacketHeader heartBeatHeader = ScratchPacketHeader(12, client.clientSSRecordKeeper->baselineRecord.packetSequence, client.packetAckMaintence->currentPacketSequence,
                client.packetAckMaintence->mostRecentRecievedPacket, client.packetAckMaintence->GetAckBits(client.packetAckMaintence->mostRecentRecievedPacket));

            Payload* heartBeatPayload = ConstructAbsolutePayload(pair->second); //grabbing the value 

            ConstructPacket(heartBeatHeader, *heartBeatPayload, transmitBuf);

            int sentBytes = listeningSocket.Send(*client.clientAddress, transmitBuf, totalPacketSize);

            client.packetAckMaintence->IncrementPacketSequence();
            delete heartBeatPayload;
        }

    }
}

void ScratchNetServer::ReplicateChangeToAllClients(Snapshot changes)
{
    for (int i = 0; i < playerConnected.size(); i++)
    {
        if (!playerConnected[i]) //dont need to update a disconnected player
        {
            continue;
        }
        const int tSize = totalPacketSize;
        char transmitBuf[tSize] = { 0 };

        ClientRecord client = *GetClientRecord(i);

        ScratchPacketHeader heartBeatHeader = ScratchPacketHeader(12, client.clientSSRecordKeeper->baselineRecord.packetSequence, client.packetAckMaintence->currentPacketSequence,
            client.packetAckMaintence->mostRecentRecievedPacket, client.packetAckMaintence->GetAckBits(client.packetAckMaintence->mostRecentRecievedPacket));

        Payload* heartBeatPayload = ConstructAbsolutePayload(changes); //grabbing the value 

        ConstructPacket(heartBeatHeader, *heartBeatPayload, transmitBuf);

        Address address = *client.clientAddress;

        //int sentBytes = listeningSocket.Send(*client.clientAddress, transmitBuf, 55 + safetyBuffer); //TODO: Fix address to be able to be used to send back to a sender
        int sendBytes = sendto(listeningSocket.GetSocket(), transmitBuf, tSize, 0, (SOCKADDR*)&address.sockAddr, sizeof(address.GetSockAddrIn()));

        client.packetAckMaintence->IncrementPacketSequence();
        delete heartBeatPayload;
    }
}

void ScratchNetServer::ReplicatedChangeToOtherClients(ClientRecord ClientSentChanges, Snapshot changes, int packetCode)
{
    for (int i = 0; i < playerConnected.size(); i++)
    {
        if (!playerConnected[i]) //dont need to update a disconnected player
        {
            continue;
        }
        int tSize = totalPacketSize;
        char transmitBuf[totalPacketSize] = { 0 };

        ClientRecord client = *GetClientRecord(i);

        if (client == ClientSentChanges)
        {
            continue;
        }

        ScratchPacketHeader heartBeatHeader = ScratchPacketHeader(packetCode, client.clientSSRecordKeeper->baselineRecord.packetSequence, client.packetAckMaintence->currentPacketSequence,
            client.packetAckMaintence->mostRecentRecievedPacket, client.packetAckMaintence->GetAckBits(client.packetAckMaintence->mostRecentRecievedPacket));

        Payload* heartBeatPayload = ConstructAbsolutePayload(changes); //grabbing the value 

        ConstructPacket(heartBeatHeader, *heartBeatPayload, transmitBuf);

        Address address = *client.clientAddress; //derefernced value

        //int sentBytes = listeningSocket.Send(address, transmitBuf, 55 + safetyBuffer);
        int sendBytes = sendto(listeningSocket.GetSocket(), transmitBuf, tSize, 0, (SOCKADDR*)&address.sockAddr, sizeof(address.GetSockAddrIn()));

        client.packetAckMaintence->IncrementPacketSequence();
        delete heartBeatPayload;
    }
}

void ScratchNetServer::RelayClientPosition(ClientRecord client)
{
    const int tSize = totalPacketSize;
    char transmitBuf[tSize] = { 0 };

    ScratchPacketHeader heartBeatHeader = ScratchPacketHeader(22, client.clientSSRecordKeeper->baselineRecord.packetSequence, client.packetAckMaintence->currentPacketSequence,
        client.packetAckMaintence->mostRecentRecievedPacket, client.packetAckMaintence->GetAckBits(client.packetAckMaintence->mostRecentRecievedPacket)); //sending a packet for the purpose of updating ACK

    Payload* heartBeatPayload = ConstructAbsolutePayload(*client.clientSSRecordKeeper->baselineRecord.recordedSnapshot); //grabbing the baseline record to send to client

    ConstructPacket(heartBeatHeader, *heartBeatPayload, transmitBuf);

    Address address = *client.clientAddress; //derefernced value

    //int sentBytes = listeningSocket.Send(address, transmitBuf, 55 + safetyBuffer);
    int sendBytes = sendto(listeningSocket.GetSocket(), transmitBuf, tSize, 0, (SOCKADDR*)&address.sockAddr, sizeof(address.GetSockAddrIn())); //TODO: FIX socket send function

    client.packetAckMaintence->IncrementPacketSequence();

    delete heartBeatPayload;
}


void ScratchNetServer::SendHeartBeat()
{
    while (!shutDownRequested)
    {
        for (int i = 0; i < playerConnected.size(); i++)
        {
            if (!playerConnected[i]) //dont need to update a disconnected player
            {
                continue;
            }

            ClientRecord client = *GetClientRecord(i); //grabbing a copy of the client record to prevent it from getting entangled with the main thread 

            RelayClientPosition(client); //update the baseline for the client 
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

}

void ScratchNetServer::InitializeNewClientWithHostState(ClientRecord* newClientToInitialize)
{
    ClientRecord hostClient = *GetClientRecord(0); //generally the host is always first

    for (auto pair = hostClient.networkedObjects.begin(); pair != hostClient.networkedObjects.end(); ++pair)
    {
        char transmitBuf[totalPacketSize] = { 0 };

        ScratchPacketHeader* header = InitPacketHeaderWithoutCRC(12, newClientToInitialize->clientSSRecordKeeper->baselineRecord.packetSequence, newClientToInitialize->packetAckMaintence->currentPacketSequence,
            newClientToInitialize->packetAckMaintence->mostRecentRecievedPacket, newClientToInitialize->packetAckMaintence->GetAckBits(newClientToInitialize->packetAckMaintence->mostRecentRecievedPacket));
    
        Payload* payloadToSend = ConstructAbsolutePayload(pair->second);

        ConstructPacket(*header, *payloadToSend, transmitBuf);

        Address address = *newClientToInitialize->clientAddress;

        for (int i = 0; i < 10; i++)
        {
            int sentBytes = listeningSocket.Send(address, transmitBuf, totalPacketSize);
        }
        

        delete header;
        delete payloadToSend;
    }

}

SNS_API ScratchNetServer* InitializeScratchServer()
{
    ScratchNetServer* serverObj = new ScratchNetServer();

    serverObj->InitializeSocket(); //init sockets

    SetConsoleCtrlHandler(serverObj->ConsoleHandler, TRUE);
    serverObj->packetAckMaintence = GenerateScratchAck();
    serverObj->isHeartBeatActive = true;

    return serverObj;
}

SNS_API void BeginServerProcess(ScratchNetServer* server)
{
    server->serverThread = std::thread(&ScratchNetServer::MainProcess, server); //initalize server in the background
    server->heartBeatWorker = std::thread(&ScratchNetServer::SendHeartBeat, server); //start heart beat on new thread
}

SNS_API void ShutdownServer(ScratchNetServer* server)
{
    shutDownRequested = true;

    if (server->serverThread.joinable())
    {
        server->serverThread.join();
    }
    
    if (server->heartBeatWorker.joinable())
    {
        server->heartBeatWorker.join();
    }

    server->listeningSocket.Close();
    
}

