#pragma once

#include <string>
#include <memory>
#include <vector>
#include <cstdint>
#include "Perun/Transport/ITransport.h"
#include "Perun/Protocol/Handshake.h"
#include "Perun/Protocol/Packets.h"

namespace Perun::Client {

class Client {
public:
    Client();
    ~Client();

    bool Connect(const std::string& address, bool useTcp = false);
    void Disconnect();
    void Update();
    void Render();
    void UpdateTexture(uint32_t textureId);

    bool IsConnected() const;
    uint16_t GetCapabilities() const;

private:
    void HandlePacket(const Protocol::PacketHeader& header, const std::vector<uint8_t>& payload);
    void SendInput(const Protocol::InputEventPacket& packet);
    
    // Process keyboard/mouse input from SDL
    void ProcessInput();

    std::shared_ptr<Transport::IConnection> m_connection;
    bool m_connected;
    uint16_t m_serverCapabilities;
    
    // Frame buffer
    std::vector<uint8_t> m_currentFrame;
    int m_frameWidth;
    int m_frameHeight;
    bool m_frameReady;
    
    // SDL Texture ID (managed by Renderer)
    uint32_t m_textureId;
};

} // namespace Perun::Client
