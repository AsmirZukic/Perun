#include <iostream>
#include <memory>
#include <cstring>
#include "Perun/Server/Server.h"
#include "Perun/Transport/UnixTransport.h"
#include "Perun/Transport/TCPTransport.h"
#include "Perun/C/perun_c.h"
#include <SDL2/SDL.h>
#include "Perun/Audio/Audio.h"

// Simple callback implementation for demonstration
class DemoCallbacks : public Perun::Server::IServerCallbacks {
public:
    DemoCallbacks(PerunTexture* screen) : m_screen(screen) {}
    
    void OnClientConnected(int clientId, uint16_t capabilities) override {
        std::cout << "[Demo] Client " << clientId << " connected with caps: 0x"
                  << std::hex << capabilities << std::dec << std::endl;
    }
    
    void OnClientDisconnected(int clientId) override {
        std::cout << "[Demo] Client " << clientId << " disconnected" << std::endl;
    }
    
    void OnVideoFrameReceived(int clientId, const Perun::Protocol::VideoFramePacket& packet) override {
        // Update screen with received frame data
        // For now, assuming raw RGBA data in compressedData (no actual compression in this demo phase)
        if (m_screen && packet.compressedData.size() == 640 * 480 * 4) {
            Perun_Texture_SetData(m_screen, packet.compressedData.data(), packet.compressedData.size());
        }
        // std::cout << "[Demo] Received video frame from client " << clientId << ", size: " << packet.compressedData.size() << std::endl;
    }

    void OnAudioChunkReceived(int clientId, const Perun::Protocol::AudioChunkPacket& packet) override {
        // Handle audio
        // std::cout << "[Demo] Received audio chunk from client " << clientId << ", samples: " << packet.samples.size() << std::endl;
    }

    void OnInputReceived(int clientId, const Perun::Protocol::InputEventPacket& packet) override {
        std::cout << "[Demo] Input from client " << clientId
                  << ", buttons: 0x" << std::hex << packet.buttons << std::dec << std::endl;
        
        // Could handle input here (e.g., pass to emulator core)
    }
    
    void OnConfigReceived(int clientId, const std::vector<uint8_t>& data) override {
        std::cout << "[Demo] Config from client " << clientId
                  << ", size: " << data.size() << " bytes" << std::endl;
    }

private:
    PerunTexture* m_screen;
};

void PrintUsage(const char* progName) {
    std::cout << "Usage: " << progName << " [options]\n"
              << "Options:\n"
              << "  -u, --unix <path>      Listen on Unix socket (default: /tmp/perun.sock)\n"
              << "  -t, --tcp <addr:port>  Listen on TCP socket (e.g., 127.0.0.1:8080)\n"
              << "  -h, --help             Show this help message\n"
              << "\nExamples:\n"
              << "  " << progName << " --unix /tmp/perun.sock\n"
              << "  " << progName << " --tcp :8080\n"
              << "  " << progName << " --unix /tmp/perun.sock --tcp :8080\n";
}

int main(int argc, char* argv[]) {
    std::cout << "[PerunServer] Starting Universal Emulator Frontend Platform..." << std::endl;
    
    bool headless = false;
    std::vector<std::pair<std::string, std::string>> transports;  // (type, address)
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            PrintUsage(argv[0]);
            return 0;
        }
        else if (arg == "--headless") {
            headless = true;
        }
        else if ((arg == "-u" || arg == "--unix") && i + 1 < argc) {
            transports.push_back({"unix", argv[++i]});
        }
        else if ((arg == "-t" || arg == "--tcp") && i + 1 < argc) {
            transports.push_back({"tcp", argv[++i]});
        }
        else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            PrintUsage(argv[0]);
            return 1;
        }
    }
    
    // Default to Unix socket if no transports specified
    if (transports.empty()) {
        transports.push_back({"unix", "/tmp/perun.sock"});
    }
    
    // Initialize Engine
    if (!Perun_Init()) {
        std::cerr << "Failed to init Perun" << std::endl;
        return 1;
    }
    
    PerunWindow* window = nullptr;
    PerunTexture* screen = nullptr;
    
    if (!headless) {
        window = Perun_Window_Create("Perun Universal Frontend", 640, 480);
        if (!Perun_Window_Init(window)) {
            std::cerr << "Failed to init Window" << std::endl;
            return 1;
        }
        
        Perun_Renderer_Init();
        
        // Create texture for rendering
        screen = Perun_Texture_Create(640, 480);
    } else {
        std::cout << "[PerunServer] Running in headless mode" << std::endl;
    }
    
    if (!headless && !Perun::Audio::Init()) {
        std::cerr << "Failed to init Audio" << std::endl;
    }
    
    // Create demo frame data (gradient pattern)
    std::vector<uint8_t> frameData(640 * 480 * 4);
    for (int y = 0; y < 480; ++y) {
        for (int x = 0; x < 640; ++x) {
            int i = (y * 640 + x) * 4;
            frameData[i + 0] = x % 256;      // R
            frameData[i + 1] = y % 256;      // G
            frameData[i + 2] = (x + y) % 256; // B
            frameData[i + 3] = 255;           // A
        }
    }
    
    // Create server and setup callbacks
    Perun::Server::Server server;
    DemoCallbacks callbacks(screen);
    server.SetCallbacks(&callbacks);
    
    // Add configured transports
    for (const auto& [type, address] : transports) {
        if (type == "unix") {
            auto transport = std::make_shared<Perun::Transport::UnixTransport>();
            if (!server.AddTransport(transport, address)) {
                std::cerr << "Failed to add Unix transport: " << address << std::endl;
                return 1;
            }
        }
        else if (type == "tcp") {
            auto transport = std::make_shared<Perun::Transport::TCPTransport>();
            if (!server.AddTransport(transport, address)) {
                std::cerr << "Failed to add TCP transport: " << address << std::endl;
                return 1;
            }
        }
    }
    
    // Start server
    if (!server.Start()) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    std::cout << "[PerunServer] Server started, waiting for connections..." << std::endl;
    
    // Main loop
    bool running = true;
    int frameCount = 0;
    
    while (running) {
        // Process server events (accept connections, receive packets)
        server.Update();
        
        // Broadcast a frame every 60 frames (~1 second at 60 FPS)
        if (frameCount % 60 == 0 && server.GetClientCount() > 0) {
            Perun::Protocol::VideoFramePacket packet;
            packet.width = 640;
            packet.height = 480;
            packet.compressedData = frameData;  // In real usage, compress with LZ4
            
            server.BroadcastVideoFrame(packet);
            std::cout << "[PerunServer] Broadcasted frame to " << server.GetClientCount() << " client(s)" << std::endl;
        }
        
        if (!headless) {
            // Update texture (for now just use demo data)
            Perun_Texture_SetData(screen, frameData.data(), frameData.size());
            
            // Render
            Perun_Renderer_BeginScene();
            Perun_Renderer_DrawTexture(0, 0, 1.0f, 1.0f, screen);
            Perun_Renderer_EndScene();
            
            // Update window
            if (!Perun_Window_Update(window)) {
                std::cout << "[PerunServer] Window closed" << std::endl;
                running = false;
            }
        } else {
            // In headless mode, just sleep to simulate frame time
            SDL_Delay(16); // ~60 FPS
            
            // Basic event polling to allow graceful exit via Ctrl+C handling or similar if implemented
            // For now, we rely on signal handling or external kill for headless termination
        }
        
        frameCount++;
    }
    
    std::cout << "[PerunServer] Shutting down..." << std::endl;
    
    // Cleanup
    server.Stop();
    if (!headless) {
        Perun_Renderer_Shutdown();
        Perun_Window_Destroy(window);
    }
    Perun_Shutdown();
    
    return 0;
}
