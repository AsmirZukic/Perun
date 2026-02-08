#pragma once

#include "Perun/Transport/ITransport.h"
#include <string>

namespace Perun::Transport {

/**
 * @brief Unix domain socket connection implementation
 */
class UnixConnection : public IConnection {
public:
    explicit UnixConnection(int fd);
    ~UnixConnection() override;
    
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
 * @brief Unix domain socket transport implementation
 * 
 * Provides connection-oriented communication using Unix domain sockets.
 * Suitable for local inter-process communication with low latency.
 */
class UnixTransport : public ITransport {
public:
    UnixTransport();
    ~UnixTransport() override;
    
    bool Listen(const std::string& address) override;
    std::shared_ptr<IConnection> Accept() override;
    std::shared_ptr<IConnection> Connect(const std::string& address) override;
    void Close() override;
    bool IsListening() const override;
    int GetListenFileDescriptor() const override;
    void SetAcceptCallback(AcceptCallback callback) override;
    
private:
    int m_listenFd;
    std::string m_socketPath;
    bool m_listening;
    AcceptCallback m_acceptCallback;
};

} // namespace Perun::Transport
