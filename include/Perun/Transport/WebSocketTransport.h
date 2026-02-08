#pragma once

#include "Perun/Transport/ITransport.h"
#include <vector>
#include <string>
#include <memory>
#include <mutex>

namespace Perun {
namespace Transport {

class WebSocketConnection : public IConnection {
public:
    explicit WebSocketConnection(int fd);
    ~WebSocketConnection() override;

    ssize_t Send(const uint8_t* data, size_t length, bool reliable = true) override;
    ssize_t Receive(uint8_t* buffer, size_t maxLength) override;
    void Close() override;
    bool IsOpen() const override;
    int GetFileDescriptor() const override;
    void SetReceiveCallback(ReceiveCallback callback) override;
    void SetCloseCallback(CloseCallback callback) override;

private:
    bool PerformHandshake(); // Process m_sockBuffer for handshake
    void ProcessFrames();    // Process m_sockBuffer for frames

    int m_fd;
    bool m_open;
    bool m_handshakeComplete;
    
    std::vector<uint8_t> m_sockBuffer;   // Raw incoming data
    std::vector<uint8_t> m_frameBuffer;  // Unwrapped application data
    
    ReceiveCallback m_receiveCallback;
    CloseCallback m_closeCallback;
};

class WebSocketTransport : public ITransport {
public:
    WebSocketTransport();
    ~WebSocketTransport() override;

    bool Listen(const std::string& address) override;
    std::shared_ptr<IConnection> Accept() override;
    std::shared_ptr<IConnection> Connect(const std::string& address) override;
    void Close() override;
    bool IsListening() const override;
    int GetListenFileDescriptor() const override;
    void SetAcceptCallback(AcceptCallback callback) override;

private:
    int m_listenFd;
    bool m_listening;
    AcceptCallback m_acceptCallback;
};

} // namespace Transport
} // namespace Perun
