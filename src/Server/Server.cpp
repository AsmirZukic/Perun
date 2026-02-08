#include "Perun/Server/Server.h"
#include <iostream>
#include <algorithm>
#include <poll.h>
#include <time.h>

namespace Perun::Server {

Server::Server()
    : m_callbacks(nullptr)
    , m_nextClientId(1)
    , m_running(false)
    , m_serverCapabilities(Protocol::CAP_DELTA | Protocol::CAP_AUDIO | Protocol::CAP_DEBUG) {
}

Server::~Server() {
    Stop();
}

bool Server::AddTransport(std::shared_ptr<Transport::ITransport> transport, const std::string& address) {
    if (m_running) {
        std::cerr << "[Server] Cannot add transport while server is running" << std::endl;
        return false;
    }
    
    if (!transport->Listen(address)) {
        std::cerr << "[Server] Failed to listen on transport: " << address << std::endl;
        return false;
    }
    
    m_transports.push_back(transport);
    std::cout << "[Server] Added transport listening on: " << address << std::endl;
    return true;
}

void Server::SetCallbacks(IServerCallbacks* callbacks) {
    m_callbacks = callbacks;
}

bool Server::Start() {
    if (m_running) {
        return true;
    }
    
    if (m_transports.empty()) {
        std::cerr << "[Server] No transports configured" << std::endl;
        return false;
    }
    
    m_running = true;
    std::cout << "[Server] Started with " << m_transports.size() << " transport(s)" << std::endl;
    return true;
}

void Server::Stop() {
    if (!m_running) {
        return;
    }
    
    std::cout << "[Server] Stopping..." << std::endl;
    
    // Disconnect all clients
    for (auto& client : m_clients) {
        DisconnectClient(client);
    }
    m_clients.clear();
    
    // Close all transports
    for (auto& transport : m_transports) {
        transport->Close();
    }
    
    m_running = false;
    std::cout << "[Server] Stopped" << std::endl;
}

void Server::Update() {
    if (!m_running) {
        return;
    }
    
    // Accept new connections
    ProcessNewConnections();
    
    // Process data from existing clients
    for (auto& client : m_clients) {
        ProcessClientData(client);
    }
    
    // Remove disconnected clients
    m_clients.erase(
        std::remove_if(m_clients.begin(), m_clients.end(),
            [](const ClientState& c) { return !c.connection->IsOpen(); }),
        m_clients.end()
    );
}

void Server::ProcessNewConnections() {
    for (auto& transport : m_transports) {
        while (auto conn = transport->Accept()) {
            ClientState client;
            client.id = m_nextClientId++;
            client.connection = conn;
            client.capabilities = 0;
            client.handshakeComplete = false;
            
            m_clients.push_back(client);
            std::cout << "[Server] New connection, client ID: " << client.id << std::endl;
        }
    }
}

void Server::ProcessClientData(ClientState& client) {
    if (!client.connection->IsOpen()) {
        return;
    }
    
    uint8_t buffer[65536]; // Increased buffer size to 64KB
    bool dataReceived = false;
    
    // Drain the socket
    while (true) {
        ssize_t received = client.connection->Receive(buffer, sizeof(buffer));
        
        if (received < 0) {
            // Error handling (Receive returns -1 on error and closes connection)
            // If connection is closed, we should disconnect client state
            if (!client.connection->IsOpen()) {
               DisconnectClient(client);
               return; 
            }
            break; 
        }
        else if (received == 0) {
            // Could be EAGAIN (0) or Disconnected (0 and closed)
            if (!client.connection->IsOpen()) {
                // Disconnected
                DisconnectClient(client);
                return;
            }
            // Just no more data (EAGAIN)
            break;
        }
        
        // Append to buffer
        client.receiveBuffer.insert(client.receiveBuffer.end(), buffer, buffer + received);
        dataReceived = true;
        
        // If we received less than max buffer, socket might be empty now
        if (received < (ssize_t)sizeof(buffer)) {
            break;
        }
    }
    
    if (!dataReceived) {
        return;
    }
    
    // Handle handshake first
    if (!client.handshakeComplete) {
        if (client.receiveBuffer.size() >= 15) {  // Minimum handshake size
            auto result = Protocol::Handshake::ProcessHello(
                client.receiveBuffer.data(),
                client.receiveBuffer.size(),
                m_serverCapabilities
            );
            
            if (result.accepted) {
                // Send OK response
                auto response = Protocol::Handshake::CreateOk(result.version, result.capabilities);
                client.connection->Send(response.data(), response.size());
                
                client.capabilities = result.capabilities;
                client.handshakeComplete = true;
                client.receiveBuffer.clear();
                
                std::cout << "[Server] Client " << client.id << " handshake complete, caps: 0x"
                          << std::hex << client.capabilities << std::dec << std::endl;
                
                if (m_callbacks) {
                    m_callbacks->OnClientConnected(client.id, client.capabilities);
                }
            } else {
                // Send error response
                auto response = Protocol::Handshake::CreateError(result.error);
                client.connection->Send(response.data(), response.size());
                client.connection->Close();
                std::cerr << "[Server] Client " << client.id << " handshake failed: "
                          << result.error << std::endl;
            }
        }
        return;
    }
    
    // Process packets
    while (client.receiveBuffer.size() >= 8) {  // Minimum packet size (header)
        auto header = Protocol::PacketHeader::Deserialize(client.receiveBuffer.data());
        
        if (client.receiveBuffer.size() < 8 + header.length) {
            break;  // Wait for complete packet
        }
        
        // Extract payload
        const uint8_t* payload = client.receiveBuffer.data() + 8;
        
        // Handle packet
        HandlePacket(client, header, payload);
        
        // Remove processed packet from buffer
        client.receiveBuffer.erase(
            client.receiveBuffer.begin(),
            client.receiveBuffer.begin() + 8 + header.length
        );
    }
}

void Server::HandlePacket(ClientState& client, const Protocol::PacketHeader& header, const uint8_t* payload) {
    if (!m_callbacks) {
        return;
    }
    
    switch (header.type) {
        case Protocol::PacketType::VideoFrame: {
            auto packet = Protocol::VideoFramePacket::Deserialize(payload, header.length);
            m_callbacks->OnVideoFrameReceived(client.id, packet);
            break;
        }

        case Protocol::PacketType::AudioChunk: {
            auto packet = Protocol::AudioChunkPacket::Deserialize(payload, header.length);
            m_callbacks->OnAudioChunkReceived(client.id, packet);
            break;
        }

        case Protocol::PacketType::InputEvent: {
            auto packet = Protocol::InputEventPacket::Deserialize(payload, header.length);
            m_callbacks->OnInputReceived(client.id, packet);
            break;
        }
        
        case Protocol::PacketType::Config: {
            std::vector<uint8_t> data(payload, payload + header.length);
            m_callbacks->OnConfigReceived(client.id, data);
            break;
        }
        
        default:
            std::cerr << "[Server] Unhandled packet type: " << (int)header.type << std::endl;
            break;
    }
}

void Server::DisconnectClient(ClientState& client) {
    if (client.connection->IsOpen()) {
        client.connection->Close();
        
        if (m_callbacks && client.handshakeComplete) {
            m_callbacks->OnClientDisconnected(client.id);
        }
        
        std::cout << "[Server] Client " << client.id << " disconnected" << std::endl;
    }
}

bool Server::SendPacket(ClientState& client, Protocol::PacketType type, const std::vector<uint8_t>& payload, bool reliable) {
    if (!client.connection->IsOpen() || !client.handshakeComplete) {
        return false;
    }
    
    Protocol::PacketHeader header;
    header.type = type;
    header.flags = 0;
    header.sequence = 0;  // TODO: Implement sequence tracking
    header.length = payload.size();
    
    auto headerBytes = header.Serialize();
    
    // Combine header and payload into single buffer for WebSocket compatibility
    // (WebSocket wraps each Send() call in a separate frame)
    std::vector<uint8_t> fullPacket;
    fullPacket.reserve(headerBytes.size() + payload.size());
    fullPacket.insert(fullPacket.end(), headerBytes.begin(), headerBytes.end());
    fullPacket.insert(fullPacket.end(), payload.begin(), payload.end());
    
    ssize_t sent = client.connection->Send(fullPacket.data(), fullPacket.size(), reliable);
    
    if (!reliable && sent == 0) {
        // Dropped (expected behaviour for unreliable send on full buffer)
        return false;
    }
    
    return sent == (ssize_t)fullPacket.size();
}

bool Server::SendVideoFrame(int clientId, const Protocol::VideoFramePacket& packet) {
    auto it = std::find_if(m_clients.begin(), m_clients.end(),
        [clientId](const ClientState& c) { return c.id == clientId; });
    
    if (it == m_clients.end()) {
        return false;
    }
    
    auto payload = packet.Serialize();
    // Send unreliably (drop if full)
    return SendPacket(*it, Protocol::PacketType::VideoFrame, payload, false);
}

void Server::BroadcastVideoFrame(const Protocol::VideoFramePacket& packet, int excludeClientId) {
    auto payload = packet.Serialize();
    
    for (auto& client : m_clients) {
        if (client.handshakeComplete && client.id != excludeClientId) {
            SendPacket(client, Protocol::PacketType::VideoFrame, payload, false);
        }
    }
}

bool Server::SendAudioChunk(int clientId, const Protocol::AudioChunkPacket& packet) {
    auto it = std::find_if(m_clients.begin(), m_clients.end(),
        [clientId](const ClientState& c) { return c.id == clientId; });
    
    if (it == m_clients.end()) {
        return false;
    }
    
    auto payload = packet.Serialize();
    return SendPacket(*it, Protocol::PacketType::AudioChunk, payload);
}

void Server::BroadcastAudioChunk(const Protocol::AudioChunkPacket& packet, int excludeClientId) {
    auto payload = packet.Serialize();
    
    for (auto& client : m_clients) {
        if (client.handshakeComplete && (client.capabilities & Protocol::CAP_AUDIO) && client.id != excludeClientId) {
            SendPacket(client, Protocol::PacketType::AudioChunk, payload);
        }
    }
}

void Server::BroadcastInputEvent(const Protocol::InputEventPacket& packet, int excludeClientId) {
    auto payload = packet.Serialize();
    
    for (auto& client : m_clients) {
        if (client.handshakeComplete && client.id != excludeClientId) {
            SendPacket(client, Protocol::PacketType::InputEvent, payload);
        }
    }
}

size_t Server::GetClientCount() const {
    return m_clients.size();
}

bool Server::IsRunning() const {
    return m_running;
}

std::vector<int> Server::GetAllFileDescriptors() const {
    std::vector<int> fds;
    
    // Add all transport listen fds
    for (const auto& transport : m_transports) {
        int fd = transport->GetListenFileDescriptor();
        if (fd >= 0) {
            fds.push_back(fd);
        }
    }
    
    // Add all client connection fds
    for (const auto& client : m_clients) {
        if (client.connection && client.connection->IsOpen()) {
            int fd = client.connection->GetFileDescriptor();
            if (fd >= 0) {
                fds.push_back(fd);
            }
        }
    }
    
    return fds;
}

int Server::Poll(int timeoutMs) {
    if (!m_running) {
        return 0;
    }
    
    auto fds = GetAllFileDescriptors();
    if (fds.empty()) {
        // No file descriptors to poll, just sleep briefly
        if (timeoutMs > 0) {
            struct timespec ts;
            ts.tv_sec = timeoutMs / 1000;
            ts.tv_nsec = (timeoutMs % 1000) * 1000000;
            nanosleep(&ts, nullptr);
        }
        return 0;
    }
    
    // Build poll array
    std::vector<struct pollfd> pollFds;
    for (int fd : fds) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;  // Watch for input events
        pfd.revents = 0;
        pollFds.push_back(pfd);
    }
    
    // Poll with timeout
    int result = poll(pollFds.data(), pollFds.size(), timeoutMs);
    
    return result;
}

} // namespace Perun::Server
