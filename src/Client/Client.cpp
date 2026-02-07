#include "Perun/Client/Client.h"
#include "Perun/Transport/UnixTransport.h"
#include "Perun/Transport/TCPTransport.h"
#include "Perun/Graphics/Renderer.h"
#include "Perun/Graphics/Texture.h"
#include "Perun/Core/Window.h"
#include <iostream>
#include <SDL2/SDL.h>

namespace Perun::Client {

Client::Client() 
    : m_connected(false)
    , m_serverCapabilities(0)
    , m_frameWidth(640)
    , m_frameHeight(480)
    , m_frameReady(false)
    , m_textureId(0)
{
    // Initialize frame buffer with black
    m_currentFrame.resize(m_frameWidth * m_frameHeight * 4, 0);
}

Client::~Client() {
    Disconnect();
}

bool Client::Connect(const std::string& address, bool useTcp) {
    std::shared_ptr<Transport::ITransport> transport;
    if (useTcp) {
        transport = std::make_shared<Transport::TCPTransport>();
    } else {
        transport = std::make_shared<Transport::UnixTransport>();
    }

    m_connection = transport->Connect(address);
    if (!m_connection) {
        std::cerr << "[Client] Failed to connect to " << address << std::endl;
        return false;
    }

    // Handshake
    uint16_t myCaps = Protocol::CAP_DELTA | Protocol::CAP_AUDIO | Protocol::CAP_DEBUG;
    auto hello = Protocol::Handshake::CreateHello(1, myCaps);
    if (m_connection->Send(hello.data(), hello.size()) != hello.size()) {
        std::cerr << "[Client] Failed to send Hello" << std::endl;
        Disconnect();
        return false;
    }

    // Receive Response (blocking for simplicity on startup)
    uint8_t buffer[1024];
    int attempts = 0;
    ssize_t received = 0;
    while (attempts < 100) { // 1 second timeout
        received = m_connection->Receive(buffer, sizeof(buffer));
        if (received > 0) break;
        SDL_Delay(10);
        attempts++;
    }

    if (received <= 0) {
        std::cerr << "[Client] Handshake timeout" << std::endl;
        Disconnect();
        return false;
    }

    auto result = Protocol::Handshake::ProcessResponse(buffer, received);
    if (!result.accepted) {
        std::cerr << "[Client] Handshake rejected: " << result.error << std::endl;
        Disconnect();
        return false;
    }

    m_serverCapabilities = result.capabilities;
    m_connected = true;
    std::cout << "[Client] Connected. Caps: 0x" << std::hex << m_serverCapabilities << std::dec << std::endl;
    return true;
}

void Client::Disconnect() {
    if (m_connection) {
        m_connection->Close();
        m_connection.reset();
    }
    m_connected = false;
}

void Client::Update() {
    if (!m_connected) return;

    // Process Input
    ProcessInput();

    // Receive Data
    // We need to handle potential fragmentation later, for now assume packets fit in buffer or are small enough
    // Ideally we should have a persistent buffer like Server.cpp
    
    // TODO: Improve receive loop to handle partial packets using a persistent buffer
    // For now, simple loop
    uint8_t buffer[65536];
    while (true) {
        ssize_t received = m_connection->Receive(buffer, sizeof(buffer));
        if (received <= 0) break;

        // Simplified packet handling - assuming 1 packet per receive for now (risky but okay for MVP)
        // Creating a proper buffer handling is recommended for Phase 6 refinement
        if (received >= 8) {
             Protocol::PacketHeader header = Protocol::PacketHeader::Deserialize(buffer);
             if (received >= 8 + header.length) {
                 std::vector<uint8_t> payload(buffer + 8, buffer + 8 + header.length);
                 HandlePacket(header, payload);
             }
        }
    }
}

void Client::HandlePacket(const Protocol::PacketHeader& header, const std::vector<uint8_t>& payload) {
    if (header.type == Protocol::PacketType::VideoFrame) {
        auto packet = Protocol::VideoFramePacket::Deserialize(payload.data(), payload.size());
        
        // Handle resize if needed
        if (packet.width != m_frameWidth || packet.height != m_frameHeight) {
            m_frameWidth = packet.width;
            m_frameHeight = packet.height;
            m_currentFrame.resize(m_frameWidth * m_frameHeight * 4);
            // Recreate texture
            // TODO: Signal renderer to recreate texture
        }

        if (header.flags & Protocol::FLAG_DELTA) {
            Protocol::VideoFramePacket::ApplyDelta(m_currentFrame.data(), packet.compressedData.data(), packet.compressedData.size());
        } else {
            // Keyframe
            if (packet.compressedData.size() == m_currentFrame.size()) {
                std::copy(packet.compressedData.begin(), packet.compressedData.end(), m_currentFrame.begin());
            }
        }
        
        m_frameReady = true;
    }
}

#include <glad/glad.h> // Add glad include

void Client::UpdateTexture(uint32_t textureId) {
    if (m_frameReady && !m_currentFrame.empty()) {
        glBindTexture(GL_TEXTURE_2D, textureId);
        // Assuming RGBA format correctly matching frame buffer
        // Also check if size changed -> we might need to handle resize in main/renderer
        // For MVP assuming 640x480 or current m_frameWidth/Height match texture
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_frameWidth, m_frameHeight, GL_RGBA, GL_UNSIGNED_BYTE, m_currentFrame.data());
        m_frameReady = false; // Mark consumed? Or keep ready for redraw? 
        // If we clear it, we won't draw next frame if Update() isn't called or no new packet.
        // Better to keep it, but flagging "new data" helps performance.
        // Let's just upload.
    }
}

void Client::Render() {
    // Texture binding and update should be handled by caller or specific method
    // Render assuming texture is bound
    
    // Draw full screen quad
    float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    Perun::Renderer::BeginScene(identity); // Pass view projection
    
    float pos[2] = {0.0f, 0.0f}; // Center? Renderer coordinates are -0.5 to 0.5?
    // Renderer::DrawQuad vertices are -0.5 to 0.5.
    // If we want full screen (-1 to 1), we need size 2.0 at pos 0.0?
    // Shader: gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 0.0, 1.0);
    // a_Position is +/- 0.5.
    // Transform is Translate * Scale.
    // Identity ViewProjection.
    // So if pos=0, size=2 -> Scale=2 -> Vertices becomes +/- 1.0. 
    // This covers NDC -1 to 1.
    
    float size[2] = {2.0f, 2.0f}; // Full NDC
    float color[4] = {1,1,1,1};
    
    Perun::Renderer::DrawQuad(pos, size, color); 
    
    Perun::Renderer::EndScene();
}

void Client::ProcessInput() {
    // SDL Event loop is usually in main.cpp, but if we handle events there, we can pass them here.
    // Or we handle input state polling.
    
    // Create InputEventPacket
    Protocol::InputEventPacket packet;
    packet.buttons = 0;
    
    const Uint8* state = SDL_GetKeyboardState(NULL);
    if (state[SDL_SCANCODE_KP_PLUS]) packet.buttons |= 0x01; // Example map
    // ... map other keys
    
    if (packet.buttons != 0) {
        SendInput(packet);
    }
}

void Client::SendInput(const Protocol::InputEventPacket& packet) {
     auto payload = packet.Serialize();
     Protocol::PacketHeader header;
     header.type = Protocol::PacketType::InputEvent;
     header.length = payload.size();
     
     auto headerBytes = header.Serialize();
     m_connection->Send(headerBytes.data(), headerBytes.size());
     m_connection->Send(payload.data(), payload.size());
}

} // namespace Perun::Client
