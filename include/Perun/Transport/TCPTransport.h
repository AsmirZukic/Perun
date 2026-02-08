#pragma once

#include "Perun/Transport/ITransport.h"
#include <string>

namespace Perun::Transport {

/**
 * @brief TCP socket connection implementation
 */
class TCPConnection : public IConnection {
public:
    explicit TCPConnection(int fd);
    ~TCPConnection() override;
    
    ssize_t Send(const uint8_t* data, size_t length, bool reliable = true) override;
    ssize_t Receive(uint8_t* buffer, size_t maxLength) override;
    void Close() override;
    bool IsOpen() const override;
    int GetFileDescriptor() const override;
    void SetReceiveCallback(ReceiveCallback callback) override;
    void SetCloseCallback(CloseCallback callback) override;
    
private:
    int m_fd;
    bool m_open;
    ReceiveCallback m_receiveCallback;
    CloseCallback m_closeCallback;
};

/**
 * @brief TCP/IP socket transport implementation
 * 
 * Provides connection-oriented communication using TCP/IP sockets.
 * Suitable for network communication with TCP_NODELAY for low latency.
 */
class TCPTransport : public ITransport {
public:
    TCPTransport();
    ~TCPTransport() override;
    
    bool Listen(const std::string& address) override;
    std::shared_ptr<IConnection> Accept() override;
    std::shared_ptr<IConnection> Connect(const std::string& address) override;
    void Close() override;
    bool IsListening() const override;
    int GetListenFileDescriptor() const override;
    void SetAcceptCallback(AcceptCallback callback) override;
    
private:
    bool ParseAddress(const std::string& address, std::string& host, uint16_t& port);
    
    int m_listenFd;
    bool m_listening;
    AcceptCallback m_acceptCallback;
};

} // namespace Perun::Transport
