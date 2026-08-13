#include "client.h"
#include "../core/logger.h"
#include "../core/math/math.h"
#include <cstring>

namespace td {

bool Client::connect(const char* serverIP, unsigned short port) {
    if (m_connected) {
        disconnect();
    }
    
    if (!m_socket.create()) {
        TD_LOG_ERROR("Failed to create client socket");
        return false;
    }
    
    if (!m_socket.connect(serverIP, port)) {
        TD_LOG_ERROR("Failed to connect to server %s:%d", serverIP, port);
        m_socket.close();
        return false;
    }
    
    m_socket.setBlocking(false);
    m_socket.setNoDelay(true);
    
    // Initialize remote entities
    memset(remoteEntities, 0, sizeof(remoteEntities));
    
    m_connected = true;
    m_playerId = 0;
    m_entityId = 0;
    m_serverTick = 0;
    m_latency = 0;
    m_pingTimer = 0;
    m_inputSequence = 0;
    
    TD_LOG_INFO("Connected to server %s:%d", serverIP, port);
    
    return true;
}

void Client::disconnect() {
    if (!m_connected) {
        return;
    }
    
    // Send leave packet
    PacketHeader header;
    header.type = PacketType::ClientLeave;
    header.playerId = m_playerId;
    header.tick = 0;
    header.size = 0;
    m_socket.send(&header, sizeof(header));
    
    m_socket.close();
    m_connected = false;
    m_playerId = 0;
    m_entityId = 0;
    
    TD_LOG_INFO("Disconnected from server");
    
    if (m_onDisconnect) {
        m_onDisconnect();
    }
}

void Client::update(float dt) {
    if (!m_connected) {
        return;
    }
    
    // Receive data
    int received = m_socket.recv(m_recvBuffer, sizeof(m_recvBuffer));
    
    if (received < 0) {
        // Connection error
        m_connected = false;
        TD_LOG_ERROR("Connection lost");
        if (m_onDisconnect) {
            m_onDisconnect();
        }
        return;
    }
    
    if (received > 0) {
        // Process packets
        int offset = 0;
        while (offset + (int)sizeof(PacketHeader) <= received) {
            PacketHeader* header = (PacketHeader*)(m_recvBuffer + offset);
            int packetSize = sizeof(PacketHeader) + header->size;
            
            if (offset + packetSize > received) {
                break;
            }
            
            processPacket(m_recvBuffer + offset, packetSize);
            offset += packetSize;
        }
    }
    
    // Interpolate entities
    interpolateEntities(dt);
    
    // Send ping periodically
    m_pingTimer += dt;
    if (m_pingTimer >= 1.0f) {
        sendPing();
        m_pingTimer = 0;
    }
}

void Client::processPacket(const uint8_t* data, int size) {
    PacketHeader* header = (PacketHeader*)data;
    const uint8_t* payload = data + sizeof(PacketHeader);
    
    switch (header->type) {
        case PacketType::PlayerAssign: {
            if (header->size >= sizeof(PlayerAssignPacket)) {
                PlayerAssignPacket* assign = (PlayerAssignPacket*)payload;
                m_playerId = assign->playerId;
                m_entityId = assign->entityId;
                
                TD_LOG_INFO("Assigned player ID: %d, entity ID: %d", 
                            m_playerId, m_entityId);
                
                if (m_onConnect) {
                    m_onConnect();
                }
            }
            break;
        }
        
        case PacketType::WorldState: {
            processWorldState(payload, header->size);
            break;
        }
        
        case PacketType::ChatMessage: {
            if (header->size >= sizeof(ChatMessagePacket) && m_onChat) {
                ChatMessagePacket* chat = (ChatMessagePacket*)payload;
                chat->message[255] = '\0';
                m_onChat(chat->senderId, chat->message);
            }
            break;
        }
        
        case PacketType::Pong: {
            if (header->size >= sizeof(uint32_t)) {
                uint32_t pingTick = *(uint32_t*)payload;
                if (pingTick == m_lastPingTick) {
                    // Calculate round-trip time
                    // Note: This is simplified, real implementation would use high-precision timer
                    m_latency = m_pingTimer * 1000.0f; // Convert to ms
                }
            }
            break;
        }
        
        case PacketType::ClientLeave: {
            m_connected = false;
            TD_LOG_INFO("Server closed connection");
            if (m_onDisconnect) {
                m_onDisconnect();
            }
            break;
        }
        
        default:
            break;
    }
}

void Client::processWorldState(const uint8_t* data, int size) {
    if (size < (int)sizeof(WorldStatePacket)) {
        return;
    }
    
    WorldStatePacket* worldState = (WorldStatePacket*)data;
    m_serverTick = worldState->tick;
    
    const uint8_t* entityData = data + sizeof(WorldStatePacket);
    int remaining = size - sizeof(WorldStatePacket);
    
    for (int i = 0; i < worldState->entityCount; i++) {
        if (remaining < (int)sizeof(EntityState)) {
            break;
        }
        
        EntityState* state = (EntityState*)entityData;
        
        if (state->entityId < TD_MAX_ENTITIES) {
            RemoteEntity& remote = remoteEntities[state->entityId];
            
            // Store previous target as new prev for interpolation
            remote.prevX = remote.targetX;
            remote.prevY = remote.targetY;
            remote.prevZ = remote.targetZ;
            remote.prevRotX = remote.targetRotX;
            remote.prevRotY = remote.targetRotY;
            remote.prevRotZ = remote.targetRotZ;
            
            // Set new target
            remote.targetX = state->x;
            remote.targetY = state->y;
            remote.targetZ = state->z;
            remote.targetRotX = state->rotX;
            remote.targetRotY = state->rotY;
            remote.targetRotZ = state->rotZ;
            
            remote.velX = state->velX;
            remote.velY = state->velY;
            remote.velZ = state->velZ;
            remote.type = state->type;
            remote.active = (state->flags & 1) != 0;
            
            // Reset interpolation time
            remote.interpTime = 0;
            
            // If this is a new entity, snap to position
            if (!remote.active) {
                remote.prevX = remote.targetX;
                remote.prevY = remote.targetY;
                remote.prevZ = remote.targetZ;
                remote.x = remote.targetX;
                remote.y = remote.targetY;
                remote.z = remote.targetZ;
            }
        }
        
        entityData += sizeof(EntityState);
        remaining -= sizeof(EntityState);
    }
}

void Client::interpolateEntities(float dt) {
    for (int i = 0; i < TD_MAX_ENTITIES; i++) {
        RemoteEntity& entity = remoteEntities[i];
        
        if (!entity.active) {
            continue;
        }
        
        entity.interpTime += dt;
        
        // Calculate interpolation factor
        float t = entity.interpTime / m_interpDuration;
        t = clamp(t, 0.0f, 1.0f);
        
        // Smooth step for nicer interpolation
        t = smoothStep(0.0f, 1.0f, t);
        
        // Interpolate position
        entity.x = td::lerp(entity.prevX, entity.targetX, t);
        entity.y = td::lerp(entity.prevY, entity.targetY, t);
        entity.z = td::lerp(entity.prevZ, entity.targetZ, t);
        
        // Interpolate rotation (simple lerp, would use slerp for quaternions)
        entity.rotX = lerpAngle(entity.prevRotX, entity.targetRotX, t);
        entity.rotY = lerpAngle(entity.prevRotY, entity.targetRotY, t);
        entity.rotZ = lerpAngle(entity.prevRotZ, entity.targetRotZ, t);
    }
}

void Client::sendInput(const ClientInput& input) {
    if (!m_connected) {
        return;
    }
    
    ClientInput inputCopy = input;
    inputCopy.sequence = m_inputSequence++;
    
    PacketHeader header;
    header.type = PacketType::InputState;
    header.playerId = m_playerId;
    header.tick = m_serverTick;
    header.size = sizeof(ClientInput);
    
    memcpy(m_sendBuffer, &header, sizeof(header));
    memcpy(m_sendBuffer + sizeof(header), &inputCopy, sizeof(ClientInput));
    
    m_socket.send(m_sendBuffer, sizeof(header) + sizeof(ClientInput));
}

void Client::sendChat(const char* message) {
    if (!m_connected) {
        return;
    }
    
    ChatMessagePacket chat;
    chat.senderId = m_playerId;
    strncpy(chat.message, message, 255);
    chat.message[255] = '\0';
    
    PacketHeader header;
    header.type = PacketType::ChatMessage;
    header.playerId = m_playerId;
    header.tick = m_serverTick;
    header.size = sizeof(ChatMessagePacket);
    
    memcpy(m_sendBuffer, &header, sizeof(header));
    memcpy(m_sendBuffer + sizeof(header), &chat, sizeof(ChatMessagePacket));
    
    m_socket.send(m_sendBuffer, sizeof(header) + sizeof(ChatMessagePacket));
}

void Client::sendPing() {
    m_lastPingTick = m_serverTick;
    m_lastPingTime = m_pingTimer;
    
    PacketHeader header;
    header.type = PacketType::Ping;
    header.playerId = m_playerId;
    header.tick = m_serverTick;
    header.size = sizeof(uint32_t);
    
    memcpy(m_sendBuffer, &header, sizeof(header));
    memcpy(m_sendBuffer + sizeof(header), &m_lastPingTick, sizeof(uint32_t));
    
    m_socket.send(m_sendBuffer, sizeof(header) + sizeof(uint32_t));
}

void Client::getInterpolatedPosition(uint8_t entityId, float& x, float& y, float& z) const {
    if (entityId < TD_MAX_ENTITIES) {
        const RemoteEntity& entity = remoteEntities[entityId];
        x = entity.x;
        y = entity.y;
        z = entity.z;
    }
}

} // namespace td
