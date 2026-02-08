#include "Perun/Transport/WebSocketTransport.h"
#include "CryptoUtils.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <poll.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>

namespace Perun {
namespace Transport {

// --- WebSocketConnection ---

WebSocketConnection::WebSocketConnection(int fd)
    : m_fd(fd), m_open(true), m_handshakeComplete(false), m_receiveCallback(nullptr), m_closeCallback(nullptr) {
    
    // Non-blocking
    int flags = fcntl(m_fd, F_GETFL, 0);
    fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);
    
    // No Delay
    int opt = 1;
    setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
}

WebSocketConnection::~WebSocketConnection() {
    Close();
}

void WebSocketConnection::Close() {
    if (m_open) {
        m_open = false;
        close(m_fd);
        m_fd = -1;
        if (m_closeCallback) {
            m_closeCallback();
        }
    }
}

bool WebSocketConnection::IsOpen() const {
    return m_open;
}

int WebSocketConnection::GetFileDescriptor() const {
    return m_fd;
}

void WebSocketConnection::SetReceiveCallback(ReceiveCallback callback) {
    m_receiveCallback = callback;
}

void WebSocketConnection::SetCloseCallback(CloseCallback callback) {
    m_closeCallback = callback;
}

ssize_t WebSocketConnection::Send(const uint8_t* data, size_t length, bool reliable) {
    if (!m_open || !m_handshakeComplete) {
        return -1;
    }

    // For unreliable sending (VideoFrames), check queue size BEFORE allocating frame
    if (!reliable) {
        int unsent = 0;
        if (ioctl(m_fd, SIOCOUTQ, &unsent) == 0) {
            // If more than 64KB (approx 8 video frames) is pending, drop this frame
            if (unsent > 65536) {
                return 0;
            }
        } else {
             return 0;
        }
    }

    // Wrap in WebSocket Frame (Binary 0x82)
    std::vector<uint8_t> frame;
    frame.push_back(0x82); // FIN + Binary

    if (length < 126) {
        frame.push_back((uint8_t)length);
    } else if (length < 65536) {
        frame.push_back(126);
        frame.push_back((length >> 8) & 0xFF);
        frame.push_back(length & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back((length >> (i * 8)) & 0xFF);
        }
    }

    // Payload
    frame.insert(frame.end(), data, data + length);

    // Send
    ssize_t totalSent = 0;
    const uint8_t* p = frame.data();
    size_t left = frame.size();
    
    while (left > 0) {
        ssize_t sent = send(m_fd, p, left, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer full
                if (!reliable) {
                    // For unreliable sends: if we already sent partial data, we have a problem
                    // But if we haven't sent anything yet, just drop the frame
                    if (totalSent == 0) {
                        return 0; // Drop frame, no blocking
                    }
                    // Partial send already occurred - must complete to avoid corruption
                    // Use a very short wait (1ms) and try once more
                    struct pollfd pfd;
                    pfd.fd = m_fd;
                    pfd.events = POLLOUT;
                    pfd.revents = 0;
                    int result = poll(&pfd, 1, 1); // 1ms max wait
                    if (result <= 0) {
                        // Can't complete - close to avoid corruption
                        Close();
                        return -1;
                    }
                    continue;
                }
                // For reliable sends, wait until we can send (but with short timeout)
                struct pollfd pfd;
                pfd.fd = m_fd;
                pfd.events = POLLOUT;
                pfd.revents = 0;
                int result = poll(&pfd, 1, 10); // 10ms max wait for reliable
                if (result <= 0) {
                    Close();
                    return -1;
                }
                continue; 
            }
            Close();
            return -1;
        }
        p += sent;
        left -= sent;
        totalSent += sent;
    }
    
    return length; // Return logical bytes sent
}


ssize_t WebSocketConnection::Receive(uint8_t* buffer, size_t maxLength) {
    if (!m_open) return -1;
    
    // 1. If we have buffered frame data, return it
    if (!m_frameBuffer.empty()) {
        size_t toCopy = std::min(maxLength, m_frameBuffer.size());
        memcpy(buffer, m_frameBuffer.data(), toCopy);
        
        // Remove copied data
        if (toCopy == m_frameBuffer.size()) {
            m_frameBuffer.clear();
        } else {
            m_frameBuffer.erase(m_frameBuffer.begin(), m_frameBuffer.begin() + toCopy);
        }
        return toCopy;
    }
    
    // 2. Read from socket
    uint8_t temp[4096];
    ssize_t received = recv(m_fd, temp, sizeof(temp), 0);
    
    if (received == 0) {
        Close();
        return 0;
    }
    
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; 
        }
        Close();
        return -1;
    }
    
    // Append to sockBuffer
    m_sockBuffer.insert(m_sockBuffer.end(), temp, temp + received);
    
    // 3. Process
    if (!m_handshakeComplete) {
        if (PerformHandshake()) {
            // Handshake done, process potential leftover frames
            ProcessFrames();
        } else {
            // Check if closed during/failed handshake
            if (!m_open) return -1;
            // Still waiting for handshake data
            return 0; 
        }
    } else {
        ProcessFrames();
    }
    
    // 4. Return any produced frame data
    if (!m_frameBuffer.empty()) {
        size_t toCopy = std::min(maxLength, m_frameBuffer.size());
        memcpy(buffer, m_frameBuffer.data(), toCopy);
        if (toCopy == m_frameBuffer.size()) {
            m_frameBuffer.clear();
        } else {
            m_frameBuffer.erase(m_frameBuffer.begin(), m_frameBuffer.begin() + toCopy);
        }
        return toCopy;
    }
    
    return 0; // No data yet
}

bool WebSocketConnection::PerformHandshake() {
    // Look for double CRLF
    std::string data((char*)m_sockBuffer.data(), m_sockBuffer.size());
    size_t pos = data.find("\r\n\r\n");
    if (pos == std::string::npos) return false;
    
    std::string request = data.substr(0, pos);
    
    // Extract Key
    std::string keyHeader = "Sec-WebSocket-Key: ";
    size_t keyStart = request.find(keyHeader);
    if (keyStart == std::string::npos) {
        // Invalid request or not a WS upgrade
        Close(); 
        return false;
    }
    
    keyStart += keyHeader.length();
    size_t keyEnd = request.find("\r\n", keyStart);
    if (keyEnd == std::string::npos) {
        Close();
        return false;
    }
    
    std::string key = request.substr(keyStart, keyEnd - keyStart);
    
    // Compute Accept
    std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    using namespace Crypto;
    SHA1 sha1;
    sha1.update(key + magic);
    std::string digest = sha1.final();
    std::string accept = Base64Encode(digest);
    
    // Response
    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << accept << "\r\n\r\n";
             
    std::string respStr = response.str();
    
    // Blocking send for handshake response (simplification)
    send(m_fd, respStr.c_str(), respStr.length(), 0);
    
    // Remove handshake from buffer
    size_t consume = pos + 4;
    m_sockBuffer.erase(m_sockBuffer.begin(), m_sockBuffer.begin() + consume);
    
    m_handshakeComplete = true;
    return true;
}

void WebSocketConnection::ProcessFrames() {
    while (m_sockBuffer.size() >= 2) {
        size_t consumed = 0;
        
        uint8_t byte0 = m_sockBuffer[0];
        uint8_t byte1 = m_sockBuffer[1];
        
        bool masked = (byte1 & 0x80) != 0;
        uint64_t payloadLen = byte1 & 0x7F;
        
        size_t headerLen = 2;
        if (payloadLen == 126) {
            if (m_sockBuffer.size() < 4) return; // Wait more data
            payloadLen = (m_sockBuffer[2] << 8) | m_sockBuffer[3];
            headerLen = 4;
        } else if (payloadLen == 127) {
            if (m_sockBuffer.size() < 10) return; // Wait more data
            payloadLen = 0;
            for(int i=0; i<8; i++) payloadLen = (payloadLen << 8) | m_sockBuffer[2+i];
            headerLen = 10;
        }
        
        uint8_t maskingKey[4];
        if (masked) {
            if (m_sockBuffer.size() < headerLen + 4) return; // Wait more
            memcpy(maskingKey, &m_sockBuffer[headerLen], 4);
            headerLen += 4;
        }
        
        if (m_sockBuffer.size() < headerLen + payloadLen) return; // Wait more
        
        // Extract payload
        const uint8_t* src = &m_sockBuffer[headerLen];
        for(size_t i=0; i<payloadLen; i++) {
            uint8_t b = masked ? (src[i] ^ maskingKey[i % 4]) : src[i];
            m_frameBuffer.push_back(b);
        }
        
        consumed = headerLen + payloadLen;
        m_sockBuffer.erase(m_sockBuffer.begin(), m_sockBuffer.begin() + consumed);
    }
}

// --- WebSocketTransport ---

WebSocketTransport::WebSocketTransport()
    : m_listenFd(-1), m_listening(false), m_acceptCallback(nullptr) {
}

WebSocketTransport::~WebSocketTransport() {
    Close();
}

bool WebSocketTransport::Listen(const std::string& address) {
    if (m_listening) return false;
    
    // Address format ":port" usually
    size_t colon = address.find(':');
    if (colon == std::string::npos) return false;
    
    int port = 0;
    try {
        port = std::stoi(address.substr(colon + 1));
    } catch(...) { return false; }
    
    m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenFd < 0) return false;
    
    int opt = 1;
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(m_listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(m_listenFd);
        return false;
    }
    
    if (listen(m_listenFd, 5) < 0) {
        close(m_listenFd);
        return false;
    }
    
    // Non-blocking
    int flags = fcntl(m_listenFd, F_GETFL, 0);
    fcntl(m_listenFd, F_SETFL, flags | O_NONBLOCK);
    
    m_listening = true;
    std::cout << "WebSocket transport listening on port " << port << std::endl;
    return true;
}

std::shared_ptr<IConnection> WebSocketTransport::Accept() {
    if (!m_listening) return nullptr;
    
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    int clientFd = accept(m_listenFd, (struct sockaddr*)&clientAddr, &clientLen);
    
    if (clientFd < 0) {
        return nullptr;
    }
    
    auto conn = std::make_shared<WebSocketConnection>(clientFd);
    if (m_acceptCallback) m_acceptCallback(conn);
    return conn;
}

std::shared_ptr<IConnection> WebSocketTransport::Connect(const std::string& address) {
    // Not implemented for client side yet
    return nullptr;
}

void WebSocketTransport::Close() {
    if (m_listening) {
        close(m_listenFd);
        m_listenFd = -1;
        m_listening = false;
    }
}

bool WebSocketTransport::IsListening() const {
    return m_listening;
}

int WebSocketTransport::GetListenFileDescriptor() const {
    return m_listenFd;
}

void WebSocketTransport::SetAcceptCallback(AcceptCallback callback) {
    m_acceptCallback = callback;
}

} // namespace Transport
} // namespace Perun
