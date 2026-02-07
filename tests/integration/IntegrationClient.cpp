#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <cstring>
#include "Perun/Transport/UnixTransport.h"
#include "Perun/Transport/TCPTransport.h"
#include "Perun/Protocol/Handshake.h"
#include "Perun/Protocol/Packets.h"

using namespace Perun;

// Simple integration client that connects to server, handshakes, and sends frames
int main(int argc, char* argv[]) {
    std::cout << "[IntegrationClient] Starting..." << std::endl;
    
    std::string address = "/tmp/perun.sock";
    bool useTcp = false;
    
    if (argc > 1 && std::string(argv[1]) == "--tcp") {
        useTcp = true;
        address = "127.0.0.1:8080";
        if (argc > 2) address = argv[2];
    } else if (argc > 1) {
        address = argv[1];
    }
    
    std::cout << "[IntegrationClient] Connecting to " << address << "..." << std::endl;
    
    std::shared_ptr<Transport::ITransport> transport;
    if (useTcp) {
        transport = std::make_shared<Transport::TCPTransport>();
    } else {
        transport = std::make_shared<Transport::UnixTransport>();
    }
    
    auto connection = transport->Connect(address);
    if (!connection) {
        std::cerr << "[IntegrationClient] Failed to connect" << std::endl;
        return 1;
    }
    
    std::cout << "[IntegrationClient] Connected! Performing handshake..." << std::endl;
    
    // Send Hello
    uint16_t myCaps = Protocol::CAP_DELTA | Protocol::CAP_AUDIO | Protocol::CAP_DEBUG;
    auto hello = Protocol::Handshake::CreateHello(1, myCaps);
    if (connection->Send(hello.data(), hello.size()) != hello.size()) {
        std::cerr << "[IntegrationClient] Failed to send Hello" << std::endl;
        return 1;
    }
    
    // Receive Response
    uint8_t buffer[1024];
    int attempts = 0;
    ssize_t received = 0;
    while (attempts < 50) { // Wait up to 500ms
        received = connection->Receive(buffer, sizeof(buffer));
        if (received > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        attempts++;
    }
    
    if (received <= 0) {
        std::cerr << "[IntegrationClient] Handshake timeout or error" << std::endl;
        return 1;
    }
    
    auto result = Protocol::Handshake::ProcessResponse(buffer, received);
    if (!result.accepted) {
        std::cerr << "[IntegrationClient] Handshake rejected: " << result.error << std::endl;
        return 1;
    }
    
    std::cout << "[IntegrationClient] Handshake OK! Caps: 0x" << std::hex << result.capabilities << std::dec << std::endl;
    
    // Send a video frame
    std::cout << "[IntegrationClient] Sending video frame..." << std::endl;
    std::vector<uint8_t> frameData(640 * 480 * 4, 0xFF); // White frame
    Protocol::VideoFramePacket packet;
    packet.width = 640;
    packet.height = 480;
    packet.compressedData = frameData;
    
    auto payload = packet.Serialize();
    
    Protocol::PacketHeader header;
    header.type = Protocol::PacketType::VideoFrame;
    header.length = payload.size();
    auto headerBytes = header.Serialize();
    
    if (connection->Send(headerBytes.data(), headerBytes.size()) != headerBytes.size()) {
        std::cerr << "[IntegrationClient] Failed to send header" << std::endl;
        return 1;
    }
    
    // Blocking send for payload (simplification for test)
    size_t totalSent = 0;
    const uint8_t* pData = payload.data();
    while (totalSent < payload.size()) {
        ssize_t sent = connection->Send(pData + totalSent, payload.size() - totalSent);
        if (sent < 0) {
            // In blocking mode this would be error, in non-blocking we should handle EAGAIN
            // But standard socket Send here is non-blocking.
            // We should use select/poll, but for simplicity let's sleep and retry
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        totalSent += sent;
    }
    
    std::cout << "[IntegrationClient] Frame sent successfully!" << std::endl;
    
    // Sleep to allow server to process
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    connection->Close();
    std::cout << "[IntegrationClient] Done." << std::endl;
    return 0;
}
