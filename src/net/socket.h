#pragma once
#include <cstdint>

namespace td {

class Socket {
public:
    Socket();
    ~Socket();
    
    // Creation
    bool create();      // Creates a TCP socket
    bool createUDP();   // Creates a UDP socket
    void close();
    
    // TCP operations
    bool bind(unsigned short port);
    bool listen(int backlog = 10);
    bool connect(const char* ip, unsigned short port);
    bool accept(Socket& clientOut);
    
    // Data transfer
    int send(const void* data, int size);
    int recv(void* buffer, int maxSize);
    int sendTo(const void* data, int size, const char* ip, unsigned short port);
    int recvFrom(void* buffer, int maxSize, char* outIP, unsigned short& outPort);
    
    // Options
    bool setBlocking(bool blocking);
    bool setReuseAddr(bool reuse);
    bool setNoDelay(bool noDelay);
    bool setBroadcast(bool broadcast);
    
    // State
    bool isValid() const;
    bool isConnected() const { return m_connected; }
    uintptr_t getHandle() const { return m_socket; }
    
    // Get last error
    int getLastError() const;
    static const char* errorToString(int error);
    
    // Initialize/cleanup Winsock
    static bool initWinsock();
    static void cleanupWinsock();
    
private:
    uintptr_t m_socket;
    bool m_connected;
    bool m_isUDP;
    
    static bool s_winsockInitialized;
    static int s_socketCount;
};

} // namespace td
