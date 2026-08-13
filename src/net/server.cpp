#include "server.h"
#include "../core/logger.h"
#include <cstring>

namespace td {

bool Server::start(unsigned short port) {
    if (m_running) {
        return true;
    }
    
    if (!m_listenSocket.create()) {
        TD_LOG_ERROR("Failed to create server socket");
        return false;
    }
    
    m_listenSocket.setReuseAddr(true);
    
    if (!m_listenSocket.bind(port)) {
        TD_LOG_ERROR("Failed to bind server to port %d", port);
        m_listenSocket.close();
        return false;
    }
    
    if (!m_listenSocket.listen(10)) {
        TD_LOG_ERROR("Failed to listen on server socket");
        m_listenSocket.close();
        return false;
    }
    
    m_listenSocket.setBlocking(false);
    
    // Initialize entities
    memset(m_entities, 0, sizeof(m_entities));
    memset(m_clients, 0, sizeof(m_clients));
    
    m_tick = 0;
    m_tickAccumulator = 0;
    m_clientCount = 0;
    m_running = true;
    
    TD_LOG_INFO("Server started on port %d", port);
    return true;
}

void Server::stop() {
    if (!m_running) {
        return;
    }
    
    // Disconnect all clients
    for (int i = 0; i < TD_MAX_CLIENTS; i++) {
        if (m_clients[i].connected) {
            sendToClient(i, PacketType::ClientLeave, nullptr, 0);
            m_clients[i].socket.close();
            m_clients[i].connected = false;
        }
    }
    
    m_listenSocket.close();
    m_running = false;
    m_clientCount = 0;
    
    TD_LOG_INFO("Server stopped");
}

void Server::update(float dt) {
    if (!m_running) {
        return;
    }
    
    // Accept new clients
    acceptClients();
    
    // Process client data
    for (int i = 0; i < TD_MAX_CLIENTS; i++) {
        if (m_clients[i].connected) {
            processClientData(i);
            
            // Check for timeout
            m_clients[i].lastRecvTime += dt;
            if (m_clients[i].lastRecvTime > 30.0f) {
                TD_LOG_INFO("Client %d timed out", m_clients[i].playerId);
                removeClient(i);
            }
        }
    }
    
    // Fixed tick rate updates
    m_tickAccumulator += dt;
    while (m_tickAccumulator >= m_tickRate) {
        m_tickAccumulator -= m_tickRate;
        m_tick++;
        
        // Broadcast world state
        broadcastWorldState();
    }
}

void Server::acceptClients() {
    Socket newClient;
    
    while (m_listenSocket.accept(newClient)) {
        // Find a free slot
        int slot = -1;
        for (int i = 0; i < TD_MAX_CLIENTS; i++) {
            if (!m_clients[i].connected) {
                slot = i;
                break;
            }
        }
        
        if (slot == -1) {
            TD_LOG_WARN("Server full, rejecting client");
            newClient.close();
            continue;
        }
        
        newClient.setBlocking(false);
        newClient.setNoDelay(true);
        
        ClientInfo& client = m_clients[slot];
        client.socket = static_cast<Socket&&>(newClient);
        client.playerId = (uint8_t)findFreePlayerId();
        client.entityId = client.playerId; // 1:1 mapping for simplicity
        client.connected = true;
        client.lastRecvTime = 0;
        client.latency = 0;
        memset(&client.lastInput, 0, sizeof(ClientInput));
        
        m_clientCount++;
        
        // Send player assignment
        PlayerAssignPacket assign;
        assign.playerId = client.playerId;
        assign.entityId = client.entityId;
        sendToClient(slot, PacketType::PlayerAssign, &assign, sizeof(assign));
        
        // Initialize entity
        m_entities[client.entityId].entityId = client.entityId;
        m_entities[client.entityId].type = 1; // Player type
        m_entities[client.entityId].flags = 1; // Active
        
        TD_LOG_INFO("Client connected: player %d", client.playerId);
        
        if (m_onConnect) {
            m_onConnect(client.playerId);
        }
    }
}

void Server::processClientData(int clientIndex) {
    ClientInfo& client = m_clients[clientIndex];
    
    int received = client.socket.recv(m_recvBuffer, sizeof(m_recvBuffer));
    
    if (received < 0) {
        // Connection error
        removeClient(clientIndex);
        return;
    }
    
    if (received > 0) {
        client.lastRecvTime = 0;
        
        // Process packets
        int offset = 0;
        while (offset + (int)sizeof(PacketHeader) <= received) {
            PacketHeader* header = (PacketHeader*)(m_recvBuffer + offset);
            int packetSize = sizeof(PacketHeader) + header->size;
            
            if (offset + packetSize > received) {
                break; // Incomplete packet
            }
            
            processPacket(clientIndex, m_recvBuffer + offset, packetSize);
            offset += packetSize;
        }
    }
}

void Server::processPacket(int clientIndex, const uint8_t* data, int size) {
    ClientInfo& client = m_clients[clientIndex];
    PacketHeader* header = (PacketHeader*)data;
    const uint8_t* payload = data + sizeof(PacketHeader);
    
    switch (header->type) {
        case PacketType::InputState: {
            if (header->size >= sizeof(ClientInput)) {
                memcpy(&client.lastInput, payload, sizeof(ClientInput));
            }
            break;
        }
        
        case PacketType::ChatMessage: {
            if (header->size > 0 && m_onMessage) {
                ChatMessagePacket* chat = (ChatMessagePacket*)payload;
                chat->message[255] = '\0';
                m_onMessage(client.playerId, chat->message);
                
                // Broadcast to other clients
                chat->senderId = client.playerId;
                for (int i = 0; i < TD_MAX_CLIENTS; i++) {
                    if (m_clients[i].connected && i != clientIndex) {
                        sendToClient(i, PacketType::ChatMessage, chat, sizeof(ChatMessagePacket));
                    }
                }
            }
            break;
        }
        
        case PacketType::Ping: {
            // Send pong back
            sendToClient(clientIndex, PacketType::Pong, &header->tick, sizeof(uint32_t));
            break;
        }
        
        case PacketType::ClientLeave: {
            removeClient(clientIndex);
            break;
        }
        
        default:
            break;
    }
}

void Server::broadcastWorldState() {
    // Build world state packet
    int offset = 0;
    
    WorldStatePacket* worldState = (WorldStatePacket*)(m_sendBuffer + sizeof(PacketHeader));
    worldState->tick = m_tick;
    worldState->entityCount = 0;
    
    offset = sizeof(WorldStatePacket);
    
    // Add active entities
    for (int i = 0; i < TD_MAX_ENTITIES; i++) {
        if (m_entities[i].flags & 1) { // Active flag
            if (offset + sizeof(EntityState) < sizeof(m_sendBuffer) - sizeof(PacketHeader)) {
                memcpy(m_sendBuffer + sizeof(PacketHeader) + offset, 
                       &m_entities[i], sizeof(EntityState));
                offset += sizeof(EntityState);
                worldState->entityCount++;
            }
        }
    }
    
    // Send to all clients
    for (int i = 0; i < TD_MAX_CLIENTS; i++) {
        if (m_clients[i].connected) {
            sendToClient(i, PacketType::WorldState, m_sendBuffer + sizeof(PacketHeader), offset);
        }
    }
}

void Server::sendPacket(Socket& sock, PacketType type, const void* data, int size) {
    PacketHeader header;
    header.type = type;
    header.playerId = 0;
    header.tick = m_tick;
    header.size = (uint16_t)size;
    
    // Copy header and data to send buffer
    memcpy(m_sendBuffer, &header, sizeof(header));
    if (data && size > 0) {
        memcpy(m_sendBuffer + sizeof(header), data, size);
    }
    
    sock.send(m_sendBuffer, sizeof(header) + size);
}

void Server::sendToClient(int clientIndex, PacketType type, const void* data, int size) {
    if (clientIndex >= 0 && clientIndex < TD_MAX_CLIENTS && m_clients[clientIndex].connected) {
        sendPacket(m_clients[clientIndex].socket, type, data, size);
    }
}

void Server::removeClient(int index) {
    if (index < 0 || index >= TD_MAX_CLIENTS || !m_clients[index].connected) {
        return;
    }
    
    ClientInfo& client = m_clients[index];
    
    // Mark entity as inactive
    if (client.entityId < TD_MAX_ENTITIES) {
        m_entities[client.entityId].flags = 0;
    }
    
    uint8_t playerId = client.playerId;
    
    client.socket.close();
    client.connected = false;
    m_clientCount--;
    
    TD_LOG_INFO("Client disconnected: player %d", playerId);
    
    if (m_onDisconnect) {
        m_onDisconnect(playerId);
    }
}

int Server::findFreePlayerId() const {
    for (int id = 1; id <= TD_MAX_CLIENTS; id++) {
        bool used = false;
        for (int i = 0; i < TD_MAX_CLIENTS; i++) {
            if (m_clients[i].connected && m_clients[i].playerId == id) {
                used = true;
                break;
            }
        }
        if (!used) {
            return id;
        }
    }
    return 0;
}

void Server::setEntityState(uint8_t entityId, const EntityState& state) {
    if (entityId < TD_MAX_ENTITIES) {
        m_entities[entityId] = state;
    }
}

const ClientInput& Server::getClientInput(uint8_t playerId) const {
    static ClientInput emptyInput;
    
    for (int i = 0; i < TD_MAX_CLIENTS; i++) {
        if (m_clients[i].connected && m_clients[i].playerId == playerId) {
            return m_clients[i].lastInput;
        }
    }
    
    return emptyInput;
}

void Server::broadcastChat(const char* message) {
    ChatMessagePacket chat;
    chat.senderId = 0; // Server message
    strncpy(chat.message, message, 255);
    chat.message[255] = '\0';
    
    for (int i = 0; i < TD_MAX_CLIENTS; i++) {
        if (m_clients[i].connected) {
            sendToClient(i, PacketType::ChatMessage, &chat, sizeof(chat));
        }
    }
}

} // namespace td
