#include "Perun/Transport/UnixTransport.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <poll.h>

namespace Perun::Transport {

// ========== UnixConnection ==========

UnixConnection::UnixConnection(int fd)
    : m_fd(fd), m_open(true), m_receiveCallback(nullptr), m_closeCallback(nullptr) {
    // Set socket to non-blocking mode
    int flags = fcntl(m_fd, F_GETFL, 0);
    fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);
}

UnixConnection::~UnixConnection() {
    if (m_open) {
        Close();
    }
}

ssize_t UnixConnection::Send(const uint8_t* data, size_t length, bool reliable) {
    if (!m_open) {
        return -1;
    }
    
    // For unreliable sending, skip if socket buffer is full
    if (!reliable) {
        struct pollfd pfd;
        pfd.fd = m_fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        
        // Check if writable immediately (0 timeout)
        int result = poll(&pfd, 1, 0);
        if (result <= 0) {
            // Socket full or error, drop packet
            return 0;
        }
    }
    
    size_t totalSent = 0;
    
    while (totalSent < length) {
        ssize_t sent = send(m_fd, data + totalSent, length - totalSent, MSG_NOSIGNAL);
        
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer full. Wait for writeability.
                struct pollfd pfd;
                pfd.fd = m_fd;
                pfd.events = POLLOUT;
                pfd.revents = 0;
                
                // Wait up to 100ms
                int result = poll(&pfd, 1, 100);
                
                if (result <= 0) {
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

ssize_t UnixConnection::Receive(uint8_t* buffer, size_t maxLength) {
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

void UnixConnection::Close() {
    if (m_open) {
        m_open = false;
        close(m_fd);
        m_fd = -1;
        
        if (m_closeCallback) {
            m_closeCallback();
        }
    }
}

bool UnixConnection::IsOpen() const {
    return m_open;
}

int UnixConnection::GetFileDescriptor() const {
    return m_fd;
}

void UnixConnection::SetReceiveCallback(ReceiveCallback callback) {
    m_receiveCallback = callback;
}

void UnixConnection::SetCloseCallback(CloseCallback callback) {
    m_closeCallback = callback;
}

// ========== UnixTransport ==========

UnixTransport::UnixTransport()
    : m_listenFd(-1), m_listening(false), m_acceptCallback(nullptr) {
}

UnixTransport::~UnixTransport() {
    Close();
}

bool UnixTransport::Listen(const std::string& address) {
    if (m_listening) {
        return false;  // Already listening
    }
    
    // Create socket
    m_listenFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_listenFd < 0) {
        std::cerr << "Failed to create Unix socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Set to non-blocking
    int flags = fcntl(m_listenFd, F_GETFL, 0);
    fcntl(m_listenFd, F_SETFL, flags | O_NONBLOCK);
    
    // Remove existing socket file if it exists
    unlink(address.c_str());
    
    // Bind to address
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, address.c_str(), sizeof(addr.sun_path) - 1);
    
    if (bind(m_listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind Unix socket: " << strerror(errno) << std::endl;
        close(m_listenFd);
        m_listenFd = -1;
        return false;
    }
    
    // Listen for connections
    if (listen(m_listenFd, 5) < 0) {
        std::cerr << "Failed to listen on Unix socket: " << strerror(errno) << std::endl;
        close(m_listenFd);
        m_listenFd = -1;
        unlink(address.c_str());
        return false;
    }
    
    m_socketPath = address;
    m_listening = true;
    
    std::cout << "Unix transport listening on: " << address << std::endl;
    return true;
}

std::shared_ptr<IConnection> UnixTransport::Accept() {
    if (!m_listening) {
        return nullptr;
    }
    
    struct sockaddr_un clientAddr;
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
    
    auto connection = std::make_shared<UnixConnection>(clientFd);
    
    if (m_acceptCallback) {
        m_acceptCallback(connection);
    }
    
    return connection;
}

std::shared_ptr<IConnection> UnixTransport::Connect(const std::string& address) {
    // Create socket
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "Failed to create Unix socket: " << strerror(errno) << std::endl;
        return nullptr;
    }
    
    // Connect to server
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, address.c_str(), sizeof(addr.sun_path) - 1);
    
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to connect to " << address << ": " << strerror(errno) << std::endl;
        close(fd);
        return nullptr;
    }
    
    return std::make_shared<UnixConnection>(fd);
}

void UnixTransport::Close() {
    if (m_listening) {
        m_listening = false;
        close(m_listenFd);
        m_listenFd = -1;
        
        // Remove socket file
        if (!m_socketPath.empty()) {
            unlink(m_socketPath.c_str());
            m_socketPath.clear();
        }
    }
}

bool UnixTransport::IsListening() const {
    return m_listening;
}

int UnixTransport::GetListenFileDescriptor() const {
    return m_listenFd;
}

void UnixTransport::SetAcceptCallback(AcceptCallback callback) {
    m_acceptCallback = callback;
}

} // namespace Perun::Transport
