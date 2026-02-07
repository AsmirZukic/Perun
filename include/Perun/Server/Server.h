#pragma once

#include "Perun/Transport/ITransport.h"
#include "Perun/Protocol/Packets.h"
#include "Perun/Protocol/Handshake.h"
#include <memory>
#include <vector>
#include <functional>

namespace Perun::Server {

/**
 * @brief Callback interface for emulator/core integration
 */
class IServerCallbacks {
public:
    virtual ~IServerCallbacks() = default;
    
    /**
     * @brief Called when a new client connects after successful handshake
     * @param clientId Unique client identifier
     * @param capabilities Negotiated capabilities for this client
     */
    virtual void OnClientConnected(int clientId, uint16_t capabilities) = 0;
    
    /**
     * @brief Called when a client disconnects
     * @param clientId Unique client identifier
     */
    virtual void OnClientDisconnected(int clientId) = 0;
    
    /**
     * @brief Called when video frame is received
     * @param clientId Client that sent the frame
     * @param packet Video frame packet
     */
    virtual void OnVideoFrameReceived(int clientId, const Protocol::VideoFramePacket& packet) = 0;

    /**
     * @brief Called when audio chunk is received
     * @param clientId Client that sent the audio
     * @param packet Audio chunk packet
     */
    virtual void OnAudioChunkReceived(int clientId, const Protocol::AudioChunkPacket& packet) = 0;

    /**
     * @brief Called when input event packet is received
     * @param clientId Client that sent the input
     * @param packet Input event packet
     */
    virtual void OnInputReceived(int clientId, const Protocol::InputEventPacket& packet) = 0;
    
    /**
     * @brief Called when configuration packet is received
     * @param clientId Client that sent the config
     * @param data Configuration data
     */
    virtual void OnConfigReceived(int clientId, const std::vector<uint8_t>& data) = 0;
};

/**
 * @brief Server class managing client connections and packet handling
 * 
 * Supports multiple transport types (Unix sockets, TCP, etc.) and handles
 * protocol-level packet serialization/deserialization.
 */
class Server {
public:
    Server();
    ~Server();
    
    /**
     * @brief Add a transport for the server to listen on
     * @param transport Transport instance (e.g., UnixTransport, TCPTransport)
     * @param address Transport-specific address
     * @return true if transport added successfully
     */
    bool AddTransport(std::shared_ptr<Transport::ITransport> transport, const std::string& address);
    
    /**
     * @brief Set callback handler for server events
     * @param callbacks Callback interface implementation
     */
    void SetCallbacks(IServerCallbacks* callbacks);
    
    /**
     * @brief Start the server (begin listening on all transports)
     * @return true if server started successfully
     */
    bool Start();
    
    /**
     * @brief Stop the server and disconnect all clients
     */
    void Stop();
    
    /**
     * @brief Process pending events (accept connections, receive data)
     * Call this regularly in your main loop
     */
    void Update();
    
    /**
     * @brief Send video frame to a specific client
     * @param clientId Client to send to
     * @param packet Video frame packet
     * @return true if sent successfully
     */
    bool SendVideoFrame(int clientId, const Protocol::VideoFramePacket& packet);
    
    /**
     * @brief Send video frame to all connected clients
     * @param packet Video frame packet
     */
    void BroadcastVideoFrame(const Protocol::VideoFramePacket& packet);
    
    /**
     * @brief Send audio chunk to a specific client
     * @param clientId Client to send to
     * @param packet Audio chunk packet
     * @return true if sent successfully
     */
    bool SendAudioChunk(int clientId, const Protocol::AudioChunkPacket& packet);
    
    /**
     * @brief Send audio chunk to all connected clients
     * @param packet Audio chunk packet
     */
    void BroadcastAudioChunk(const Protocol::AudioChunkPacket& packet);
    
    /**
     * @brief Get number of connected clients
     */
    size_t GetClientCount() const;
    
    /**
     * @brief Check if server is running
     */
    bool IsRunning() const;

private:
    struct ClientState {
        int id;
        std::shared_ptr<Transport::IConnection> connection;
        uint16_t capabilities;
        std::vector<uint8_t> receiveBuffer;
        bool handshakeComplete;
    };
    
    void ProcessNewConnections();
    void ProcessClientData(ClientState& client);
    void HandlePacket(ClientState& client, const Protocol::PacketHeader& header, const uint8_t* payload);
    void DisconnectClient(ClientState& client);
    bool SendPacket(ClientState& client, Protocol::PacketType type, const std::vector<uint8_t>& payload);
    
    std::vector<std::shared_ptr<Transport::ITransport>> m_transports;
    std::vector<ClientState> m_clients;
    IServerCallbacks* m_callbacks;
    int m_nextClientId;
    bool m_running;
    uint16_t m_serverCapabilities;
};

} // namespace Perun::Server
