#include "Perun/Transport/TCPTransport.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>

namespace Perun::Transport {

// ========== TCPConnection ==========

TCPConnection::TCPConnection(int fd)
    : m_fd(fd), m_open(true), m_receiveCallback(nullptr), m_closeCallback(nullptr) {
    // Set socket to non-blocking mode
    int flags = fcntl(m_fd, F_GETFL, 0);
    fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);
    
    // Enable TCP_NODELAY to disable Nagle's algorithm for low latency
    int nodelay = 1;
    setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    
    // Set send buffer size to 128KB to allow for bursts of video frames
    int sndbuf = 128 * 1024;
    setsockopt(m_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
}

TCPConnection::~TCPConnection() {
    if (m_open) {
        Close();
    }
}

ssize_t TCPConnection::Send(const uint8_t* data, size_t length, bool reliable) {
    if (!m_open) {
        return -1;
    }
    
    // For unreliable sending (VideoFrames), check queue size
    if (!reliable) {
        int unsent = 0;
        if (ioctl(m_fd, SIOCOUTQ, &unsent) == 0) {
            // If more than 64KB (aprox 8 video frames) is pending, drop this frame
            // This ensures we never build up more than ~130ms of latency
            if (unsent > 65536) {
                return 0;
            }
        } else {
             return 0;
        }
    }
    
    size_t totalSent = 0;
    
    while (totalSent < length) {
        ssize_t sent = send(m_fd, data + totalSent, length - totalSent, MSG_NOSIGNAL);
        
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer full. Wait for writeability.
                // We MUST finish sending data to avoid stream corruption.
                struct pollfd pfd;
                pfd.fd = m_fd;
                pfd.events = POLLOUT;
                pfd.revents = 0;
                
                // Wait up to 100ms
                int result = poll(&pfd, 1, 100);
                
                if (result <= 0) {
                    // Timeout or error during wait
                    // Failed to send complete packet - close connection to signal error
                    Close();
                    return -1;
                }
                continue;
            }
            
            if (errno == EPIPE || errno == ECONNRESET) {
                // Connection closed by peer
                Close();
            }
            return -1;
        }
        
        totalSent += sent;
    }
    
    return totalSent;
}

ssize_t TCPConnection::Receive(uint8_t* buffer, size_t maxLength) {
    if (!m_open) {
        return -1;
    }
    
    ssize_t received = recv(m_fd, buffer, maxLength, 0);
    
    if (received == 0) {
        // Connection closed by peer
        Close();
        return 0;
    }
    
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No data available (non-blocking)
            return 0;
        }
        // Other error
        Close();
        return -1;
    }
    
    return received;
}

void TCPConnection::Close() {
    if (m_open) {
        m_open = false;
        close(m_fd);
        m_fd = -1;
        
        if (m_closeCallback) {
            m_closeCallback();
        }
    }
}

bool TCPConnection::IsOpen() const {
    return m_open;
}

int TCPConnection::GetFileDescriptor() const {
    return m_fd;
}

void TCPConnection::SetReceiveCallback(ReceiveCallback callback) {
    m_receiveCallback = callback;
}

void TCPConnection::SetCloseCallback(CloseCallback callback) {
    m_closeCallback = callback;
}

// ========== TCPTransport ==========

TCPTransport::TCPTransport()
    : m_listenFd(-1), m_listening(false), m_acceptCallback(nullptr) {
}

TCPTransport::~TCPTransport() {
    Close();
}

bool TCPTransport::ParseAddress(const std::string& address, std::string& host, uint16_t& port) {
    // Format: "host:port" or ":port" (bind to all interfaces)
    size_t colonPos = address.find(':');
    if (colonPos == std::string::npos) {
        return false;
    }
    
    host = address.substr(0, colonPos);
    if (host.empty()) {
        host = "0.0.0.0";  // Bind to all interfaces
    }
    
    try {
        port = std::stoi(address.substr(colonPos + 1));
    } catch (...) {
        return false;
    }
    
    return true;
}

bool TCPTransport::Listen(const std::string& address) {
    if (m_listening) {
        return false;  // Already listening
    }
    
    std::string host;
    uint16_t port;
    if (!ParseAddress(address, host, port)) {
        std::cerr << "Invalid address format: " << address << std::endl;
        return false;
    }
    
    // Create socket
    m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenFd < 0) {
        std::cerr << "Failed to create TCP socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Set to non-blocking
    int flags = fcntl(m_listenFd, F_GETFL, 0);
    fcntl(m_listenFd, F_SETFL, flags | O_NONBLOCK);
    
    // Set SO_REUSEADDR to allow quick restart
    int reuse = 1;
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    // Bind to address
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            std::cerr << "Invalid IP address: " << host << std::endl;
            close(m_listenFd);
            m_listenFd = -1;
            return false;
        }
    }
    
    if (bind(m_listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind TCP socket: " << strerror(errno) << std::endl;
        close(m_listenFd);
        m_listenFd = -1;
        return false;
    }
    
    // Listen for connections
    if (listen(m_listenFd, 5) < 0) {
        std::cerr << "Failed to listen on TCP socket: " << strerror(errno) << std::endl;
        close(m_listenFd);
        m_listenFd = -1;
        return false;
    }
    
    m_listening = true;
    
    std::cout << "TCP transport listening on: " << host << ":" << port << std::endl;
    return true;
}

std::shared_ptr<IConnection> TCPTransport::Accept() {
    if (!m_listening) {
        return nullptr;
    }
    
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    
    int clientFd = accept(m_listenFd, (struct sockaddr*)&clientAddr, &clientLen);
    
    if (clientFd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No pending connections (non-blocking)
            return nullptr;
        }
        std::cerr << "Failed to accept connection: " << strerror(errno) << std::endl;
        return nullptr;
    }
    
    auto connection = std::make_shared<TCPConnection>(clientFd);
    
    if (m_acceptCallback) {
        m_acceptCallback(connection);
    }
    
    return connection;
}

std::shared_ptr<IConnection> TCPTransport::Connect(const std::string& address) {
    std::string host;
    uint16_t port;
    if (!ParseAddress(address, host, port)) {
        std::cerr << "Invalid address format: " << address << std::endl;
        return nullptr;
    }
    
    // Create socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "Failed to create TCP socket: " << strerror(errno) << std::endl;
        return nullptr;
    }
    
    // Connect to server
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "Invalid IP address: " << host << std::endl;
        close(fd);
        return nullptr;
    }
    
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to connect to " << address << ": " << strerror(errno) << std::endl;
        close(fd);
        return nullptr;
    }
    
    return std::make_shared<TCPConnection>(fd);
}

void TCPTransport::Close() {
    if (m_listening) {
        m_listening = false;
        close(m_listenFd);
        m_listenFd = -1;
    }
}

bool TCPTransport::IsListening() const {
    return m_listening;
}

int TCPTransport::GetListenFileDescriptor() const {
    return m_listenFd;
}

void TCPTransport::SetAcceptCallback(AcceptCallback callback) {
    m_acceptCallback = callback;
}

} // namespace Perun::Transport
