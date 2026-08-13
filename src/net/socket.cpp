#include "socket.h"
#include "../core/logger.h"
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

namespace td {

bool Socket::s_winsockInitialized = false;
int Socket::s_socketCount = 0;

bool Socket::initWinsock() {
    if (s_winsockInitialized) {
        return true;
    }
    
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    if (result != 0) {
        TD_LOG_ERROR("WSAStartup failed: %d", result);
        return false;
    }
    
    s_winsockInitialized = true;
    TD_LOG_INFO("Winsock initialized");
    return true;
}

void Socket::cleanupWinsock() {
    if (s_winsockInitialized && s_socketCount == 0) {
        WSACleanup();
        s_winsockInitialized = false;
        TD_LOG_INFO("Winsock cleaned up");
    }
}

Socket::Socket() : m_socket(INVALID_SOCKET), m_connected(false), m_isUDP(false) {
    initWinsock();
}

Socket::~Socket() {
    close();
}

bool Socket::create() {
    close();
    
    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if (m_socket == INVALID_SOCKET) {
        TD_LOG_ERROR("Failed to create TCP socket: %d", WSAGetLastError());
        return false;
    }
    
    m_isUDP = false;
    s_socketCount++;
    return true;
}

bool Socket::createUDP() {
    close();
    
    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    if (m_socket == INVALID_SOCKET) {
        TD_LOG_ERROR("Failed to create UDP socket: %d", WSAGetLastError());
        return false;
    }
    
    m_isUDP = true;
    s_socketCount++;
    return true;
}

void Socket::close() {
    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        m_connected = false;
        s_socketCount--;
    }
}

bool Socket::bind(unsigned short port) {
    if (!isValid()) return false;
    
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    int result = ::bind(m_socket, (sockaddr*)&addr, sizeof(addr));
    
    if (result == SOCKET_ERROR) {
        TD_LOG_ERROR("bind failed: %d", WSAGetLastError());
        return false;
    }
    
    return true;
}

bool Socket::listen(int backlog) {
    if (!isValid()) return false;
    
    int result = ::listen(m_socket, backlog);
    
    if (result == SOCKET_ERROR) {
        TD_LOG_ERROR("listen failed: %d", WSAGetLastError());
        return false;
    }
    
    return true;
}

bool Socket::connect(const char* ip, unsigned short port) {
    if (!isValid()) return false;
    
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    // Convert IP string to address
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        TD_LOG_ERROR("Invalid IP address: %s", ip);
        return false;
    }
    
    int result = ::connect(m_socket, (sockaddr*)&addr, sizeof(addr));
    
    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            TD_LOG_ERROR("connect failed: %d", error);
            return false;
        }
    }
    
    m_connected = true;
    return true;
}

bool Socket::accept(Socket& clientOut) {
    if (!isValid()) return false;
    
    sockaddr_in clientAddr;
    int addrLen = sizeof(clientAddr);
    
    SOCKET clientSocket = ::accept(m_socket, (sockaddr*)&clientAddr, &addrLen);
    
    if (clientSocket == INVALID_SOCKET) {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            TD_LOG_ERROR("accept failed: %d", error);
        }
        return false;
    }
    
    clientOut.close();
    clientOut.m_socket = clientSocket;
    clientOut.m_connected = true;
    clientOut.m_isUDP = false;
    s_socketCount++;
    
    return true;
}

int Socket::send(const void* data, int size) {
    if (!isValid()) return -1;
    
    int result = ::send(m_socket, (const char*)data, size, 0);
    
    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            TD_LOG_ERROR("send failed: %d", error);
            return -1;
        }
        return 0;
    }
    
    return result;
}

int Socket::recv(void* buffer, int maxSize) {
    if (!isValid()) return -1;
    
    int result = ::recv(m_socket, (char*)buffer, maxSize, 0);
    
    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            if (error != WSAECONNRESET) {
                TD_LOG_ERROR("recv failed: %d", error);
            }
            return -1;
        }
        return 0;
    }
    
    if (result == 0) {
        // Connection closed
        m_connected = false;
        return -1;
    }
    
    return result;
}

int Socket::sendTo(const void* data, int size, const char* ip, unsigned short port) {
    if (!isValid()) return -1;
    
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    
    int result = ::sendto(m_socket, (const char*)data, size, 0, 
                          (sockaddr*)&addr, sizeof(addr));
    
    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            TD_LOG_ERROR("sendto failed: %d", error);
            return -1;
        }
        return 0;
    }
    
    return result;
}

int Socket::recvFrom(void* buffer, int maxSize, char* outIP, unsigned short& outPort) {
    if (!isValid()) return -1;
    
    sockaddr_in addr;
    int addrLen = sizeof(addr);
    
    int result = ::recvfrom(m_socket, (char*)buffer, maxSize, 0,
                            (sockaddr*)&addr, &addrLen);
    
    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            TD_LOG_ERROR("recvfrom failed: %d", error);
            return -1;
        }
        return 0;
    }
    
    if (outIP) {
        inet_ntop(AF_INET, &addr.sin_addr, outIP, 46);
    }
    outPort = ntohs(addr.sin_port);
    
    return result;
}

bool Socket::setBlocking(bool blocking) {
    if (!isValid()) return false;
    
    u_long mode = blocking ? 0 : 1;
    int result = ioctlsocket(m_socket, FIONBIO, &mode);
    
    if (result == SOCKET_ERROR) {
        TD_LOG_ERROR("setBlocking failed: %d", WSAGetLastError());
        return false;
    }
    
    return true;
}

bool Socket::setReuseAddr(bool reuse) {
    if (!isValid()) return false;
    
    int value = reuse ? 1 : 0;
    int result = setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, 
                            (const char*)&value, sizeof(value));
    
    if (result == SOCKET_ERROR) {
        TD_LOG_ERROR("setReuseAddr failed: %d", WSAGetLastError());
        return false;
    }
    
    return true;
}

bool Socket::setNoDelay(bool noDelay) {
    if (!isValid()) return false;
    
    int value = noDelay ? 1 : 0;
    int result = setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY,
                            (const char*)&value, sizeof(value));
    
    if (result == SOCKET_ERROR) {
        TD_LOG_ERROR("setNoDelay failed: %d", WSAGetLastError());
        return false;
    }
    
    return true;
}

bool Socket::setBroadcast(bool broadcast) {
    if (!isValid()) return false;
    
    int value = broadcast ? 1 : 0;
    int result = setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST,
                            (const char*)&value, sizeof(value));
    
    if (result == SOCKET_ERROR) {
        TD_LOG_ERROR("setBroadcast failed: %d", WSAGetLastError());
        return false;
    }
    
    return true;
}

bool Socket::isValid() const {
    return m_socket != INVALID_SOCKET;
}

int Socket::getLastError() const {
    return WSAGetLastError();
}

const char* Socket::errorToString(int error) {
    switch (error) {
        case WSAEWOULDBLOCK: return "Would block";
        case WSAECONNREFUSED: return "Connection refused";
        case WSAECONNRESET: return "Connection reset";
        case WSAETIMEDOUT: return "Timeout";
        case WSAEADDRINUSE: return "Address in use";
        case WSAENOTCONN: return "Not connected";
        default: return "Unknown error";
    }
}

} // namespace td
