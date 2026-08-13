#pragma once
#include "socket.h"
#include "../core/math/vec3.h"
#include <cstdint>

#define TD_MAX_CLIENTS 32
#define TD_MAX_ENTITIES 256

namespace td {

enum class PacketType : uint8_t {
    None = 0,
    ClientJoin = 1,
    ClientLeave = 2,
    InputState = 3,
    WorldState = 4,
    ChatMessage = 5,
    PlayerAssign = 6,
    ServerTick = 7,
    EntityCreate = 8,
    EntityDestroy = 9,
    EntityUpdate = 10,
    Ping = 11,
    Pong = 12
};

#pragma pack(push, 1)

struct PacketHeader {
    PacketType type;
    uint8_t playerId;
    uint32_t tick;
    uint16_t size;  // Payload size after header
};

struct ClientInput {
    uint8_t keys[4] = {};  // left, right, up, down
    float mouseX = 0;
    float mouseY = 0;
    uint8_t mouseButtons = 0;
    uint32_t sequence = 0;
};

struct EntityState {
    uint8_t entityId;
    float x, y, z;
    float velX, velY, velZ;
    float rotX, rotY, rotZ;
    uint8_t type;
    uint8_t flags;
};

struct WorldStatePacket {
    uint32_t tick;
    uint8_t entityCount;
    // Followed by EntityState[entityCount]
};

struct PlayerAssignPacket {
    uint8_t playerId;
    uint8_t entityId;
};

struct ChatMessagePacket {
    uint8_t senderId;
    char message[256];
};

#pragma pack(pop)

struct ClientInfo {
    Socket socket;
    char ip[46] = {};
    unsigned short port = 0;
    uint8_t playerId = 0;
    uint8_t entityId = 0;
    bool connected = false;
    ClientInput lastInput;
    float lastRecvTime = 0;
    float latency = 0;
    uint32_t lastAckTick = 0;
};

class Server {
public:
    bool start(unsigned short port);
    void stop();
    void update(float dt);
    
    bool isRunning() const { return m_running; }
    int getClientCount() const { return m_clientCount; }
    uint32_t getTick() const { return m_tick; }
    
    // Callbacks
    using ClientConnectCallback = void(*)(uint8_t playerId);
    using ClientDisconnectCallback = void(*)(uint8_t playerId);
    using MessageCallback = void(*)(uint8_t playerId, const char* message);
    
    void setClientConnectCallback(ClientConnectCallback cb) { m_onConnect = cb; }
    void setClientDisconnectCallback(ClientDisconnectCallback cb) { m_onDisconnect = cb; }
    void setMessageCallback(MessageCallback cb) { m_onMessage = cb; }
    
    // Entity management
    void setEntityState(uint8_t entityId, const EntityState& state);
    const EntityState& getEntityState(uint8_t entityId) const { return m_entities[entityId]; }
    
    // Get client input
    const ClientInput& getClientInput(uint8_t playerId) const;
    
    // Broadcast message
    void broadcastChat(const char* message);
    
private:
    void acceptClients();
    void processClientData(int clientIndex);
    void processPacket(int clientIndex, const uint8_t* data, int size);
    void broadcastWorldState();
    void sendPacket(Socket& sock, PacketType type, const void* data, int size);
    void sendToClient(int clientIndex, PacketType type, const void* data, int size);
    void removeClient(int index);
    int findFreePlayerId() const;
    
    Socket m_listenSocket;
    ClientInfo m_clients[TD_MAX_CLIENTS];
    EntityState m_entities[TD_MAX_ENTITIES];
    int m_clientCount = 0;
    uint32_t m_tick = 0;
    float m_tickRate = 1.0f / 60.0f;
    float m_tickAccumulator = 0;
    bool m_running = false;
    
    uint8_t m_sendBuffer[4096];
    uint8_t m_recvBuffer[4096];
    
    ClientConnectCallback m_onConnect = nullptr;
    ClientDisconnectCallback m_onDisconnect = nullptr;
    MessageCallback m_onMessage = nullptr;
};

} // namespace td
