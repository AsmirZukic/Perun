#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <algorithm>
#include "Perun/Server/Server.h"
#include "Perun/Transport/UnixTransport.h"
#include "Perun/Transport/TCPTransport.h"
#include "Perun/Transport/WebSocketTransport.h"

using namespace Perun;

class RelayCallbacks : public Server::IServerCallbacks {
public:
    RelayCallbacks() {}
    
    void OnClientConnected(int clientId, uint16_t capabilities) override {
        std::cout << "[Server] Client " << clientId << " connected (caps: 0x"
                  << std::hex << capabilities << std::dec << ")" << std::endl;
    }
    
    void OnClientDisconnected(int clientId) override {
        std::cout << "[Server] Client " << clientId << " disconnected" << std::endl;
    }
    
    void OnVideoFrameReceived(int clientId, const Protocol::VideoFramePacket& packet) override {
        if (m_server) {
            m_server->BroadcastVideoFrame(packet, clientId);
        }
    }

    void OnAudioChunkReceived(int clientId, const Protocol::AudioChunkPacket& packet) override {
        if (m_server) {
            m_server->BroadcastAudioChunk(packet, clientId);
        }
    }

    void OnInputReceived(int clientId, const Protocol::InputEventPacket& packet) override {
        if (m_server) {
            m_server->BroadcastInputEvent(packet, clientId);
        }
    }
    
    void OnConfigReceived(int clientId, const std::vector<uint8_t>& data) override {
        // Handle config if needed
    }

    void SetServer(Server::Server* server) { m_server = server; }

private:
    Server::Server* m_server = nullptr;
};

void PrintUsage(const char* progName) {
    std::cout << "Usage: " << progName << " [options]\n"
              << "Options:\n"
              << "  -u, --unix <path>      Listen on Unix socket (default: /tmp/perun.sock)\n"
              << "  -t, --tcp <addr:port>  Listen on TCP socket (e.g., 127.0.0.1:8080)\n"
              << "  -w, --ws <port>        Listen on WebSocket port (e.g., :8081)\n"
              << "  -h, --help             Show this help message\n";
}

int main(int argc, char* argv[]) {
    std::cout << "[PerunServer] Starting Headless Relay Platform..." << std::endl;
    
    std::vector<std::pair<std::string, std::string>> transports;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            PrintUsage(argv[0]);
            return 0;
        }
        else if ((arg == "-u" || arg == "--unix") && i + 1 < argc) {
            transports.push_back({"unix", argv[++i]});
        }
        else if ((arg == "-t" || arg == "--tcp") && i + 1 < argc) {
            transports.push_back({"tcp", argv[++i]});
        }
        else if ((arg == "-w" || arg == "--ws") && i + 1 < argc) {
            transports.push_back({"ws", argv[++i]});
        }
    }
    
    if (transports.empty()) {
        transports.push_back({"unix", "/tmp/perun.sock"});
        transports.push_back({"tcp", ":8080"});
    }
    
    Server::Server server;
    RelayCallbacks callbacks;
    callbacks.SetServer(&server);
    server.SetCallbacks(&callbacks);
    
    for (const auto& [type, address] : transports) {
        std::shared_ptr<Transport::ITransport> transport;
        if (type == "unix") transport = std::make_shared<Transport::UnixTransport>();
        else if (type == "tcp") transport = std::make_shared<Transport::TCPTransport>();
        else if (type == "ws") transport = std::make_shared<Transport::WebSocketTransport>();
        
        if (transport && !server.AddTransport(transport, address)) {
            std::cerr << "[Server] Failed to add " << type << " transport: " << address << std::endl;
            return 1;
        }
    }
    
    if (!server.Start()) {
        std::cerr << "[Server] Failed to start server" << std::endl;
        return 1;
    }
    
    std::cout << "[Server] Running. Press Ctrl+C to stop." << std::endl;
    
    while (server.IsRunning()) {
        server.Update();
        server.Poll(5); // Wait up to 5ms for events
    }
    
    server.Stop();
    return 0;
}

