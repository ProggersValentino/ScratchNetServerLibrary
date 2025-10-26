#pragma once
#include "Socket.h"
#include "ScratchAck.h"
#include "ClientRecord.h"

#include <Windows.h>
#include <vector>
#include <array>
#include <queue>
#include <thread>
#include <unordered_map>
#include <atomic>

#define SNS_EXPORTS

#ifdef SNS_EXPORTS
#define SNS_API __declspec(dllexport)
#else
#define SNS_API_declspec(dllimport)
#endif


const int maxPlayers = 4;

extern std::atomic<bool> shutDownRequested;


class ScratchNetServer
{
public:
    ScratchNetServer();

    void InitializeSocket(); //initialize the sockets to ensure the server can communicate 

    void MainProcess();

    //returns the position the player is located otherwise -1
    int FindPlayer(Address player);
    int FindFreeClientIndex() const;
    bool IsClientConnected(int clientIndex);
    const Address& GetClientAddress(int clientIndex);
    ClientRecord* GetClientRecord(int clientIndex);

    bool TryToAddPlayer(Address* potentialPlayer, ClientRecord*& OUTRecord);

    /// <summary>
    /// will return a code based of the connection status of the player 
    /// </summary>
    /// <param name="clientAddress"></param>
    /// <param name="OUTRecord"></param>
    /// <returns>returns either 0, 1, -1 
    /// 0 -> Player has been connected for a while
    /// 1 -> New player is connecting
    /// -1 -> Can't connect player to lobby
    /// </returns>
    int DetermineClient(Address* clientAddress, ClientRecord*& OUTRecord);


    bool UpdateClientObjects(ClientRecord* clientToUpdate, Snapshot selectedObjectToUpdate);

    //to update all the connected clients to the current state of the client that sent a packet
    void UpdateLocalNetworkedObjectsOnClientRecords(ClientRecord clientWithUpdates, Snapshot ObjectChanges);

    //sends changes to all clients connected
    void ReplicateChangeGroupToAllClients();

    void ReplicateChangeToAllClients(Snapshot changes);

    //used if we have a main sender and only need to update the other clients connected
    void ReplicatedChangeToOtherClients(ClientRecord ClientSentChanges, Snapshot changes, int packetCode);

    //to synchronize the given client's position 
    void RelayClientPosition(ClientRecord client);

    //to send each client a packet to update the baseline under packet code 22 every 250ms 
    void SendHeartBeat();

    void InitializeNewClientWithHostState(ClientRecord* newClientToInitialize);

    static BOOL ConsoleHandler(DWORD signal) {
        switch (signal) {
        case CTRL_C_EVENT:
            case CTRL_CLOSE_EVENT:
            std::cout << "\nGracefully shutting down...\n";
            shutDownRequested = true; // signal threads to stop
            return TRUE;     // tell the OS you handled it
        default:
            return FALSE;
        }
    }

public:
    Socket listeningSocket;

    ScratchAck* packetAckMaintence;

    std::unordered_map<int, Snapshot> networkedObjects;

    int numOfConnectedClients;

    std::array<bool, maxPlayers> playerConnected;
    std::array<ClientRecord*, maxPlayers> playerRecord;

    bool isHeartBeatActive = false;
    bool serverRunning = true;

    std::thread serverThread;
    std::thread heartBeatWorker;

    

};

extern "C"
{
    //set up sockets and other related 
    SNS_API ScratchNetServer* InitializeScratchServer();

    //start the main server process 
    SNS_API void BeginServerProcess(ScratchNetServer* server);

    //gracefully shutdown the server and any other processes running in the background
    SNS_API void ShutdownServer(ScratchNetServer* server);
}