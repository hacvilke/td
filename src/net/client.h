#pragma once
#include "socket.h"
#include "server.h"
#include <cstdint>

namespace td {

class Client {
public:
    bool connect(const char* serverIP, unsigned short port);
    void disconnect();
    void update(float dt);
    
    bool isConnected() const { return m_connected; }
    uint8_t getPlayerId() const { return m_playerId; }
    uint8_t getEntityId() const { return m_entityId; }
    uint32_t getServerTick() const { return m_serverTick; }
    float getLatency() const { return m_latency; }
    
    // Remote entity state (interpolated)
    struct RemoteEntity {
        float x, y, z;
        float velX, velY, velZ;
        float rotX, rotY, rotZ;
        uint8_t type;
        bool active;
        
        // For interpolation
        float prevX, prevY, prevZ;
        float targetX, targetY, targetZ;
        float prevRotX, prevRotY, prevRotZ;
        float targetRotX, targetRotY, targetRotZ;
        float interpTime;
    };
    
    RemoteEntity remoteEntities[TD_MAX_ENTITIES];
    
    // Send input to server
    void sendInput(const ClientInput& input);
    
    // Send chat message
    void sendChat(const char* message);
    
    // Callbacks
    using ConnectionCallback = void(*)();
    using DisconnectionCallback = void(*)();
    using ChatCallback = void(*)(uint8_t senderId, const char* message);
    
    void setConnectionCallback(ConnectionCallback cb) { m_onConnect = cb; }
    void setDisconnectionCallback(DisconnectionCallback cb) { m_onDisconnect = cb; }
    void setChatCallback(ChatCallback cb) { m_onChat = cb; }
    
    // Get interpolated position
    void getInterpolatedPosition(uint8_t entityId, float& x, float& y, float& z) const;
    
private:
    void processPacket(const uint8_t* data, int size);
    void processWorldState(const uint8_t* data, int size);
    void interpolateEntities(float dt);
    void sendPing();
    
    Socket m_socket;
    bool m_connected = false;
    uint8_t m_playerId = 0;
    uint8_t m_entityId = 0;
    uint32_t m_serverTick = 0;
    float m_latency = 0;
    float m_interpTime = 0;
    float m_pingTimer = 0;
    uint32_t m_lastPingTick = 0;
    float m_lastPingTime = 0;
    
    uint32_t m_inputSequence = 0;
    
    uint8_t m_sendBuffer[4096];
    uint8_t m_recvBuffer[4096];
    
    // Interpolation settings
    float m_interpDuration = 0.1f; // 100ms interpolation buffer
    
    ConnectionCallback m_onConnect = nullptr;
    DisconnectionCallback m_onDisconnect = nullptr;
    ChatCallback m_onChat = nullptr;
};

} // namespace td
