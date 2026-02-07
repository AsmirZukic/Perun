#include "Perun/Protocol/Handshake.h"
#include <cstring>

namespace Perun::Protocol {

std::vector<uint8_t> Handshake::CreateHello(uint16_t version, uint16_t caps) {
    std::vector<uint8_t> hello;
    hello.reserve(15);
    
    // Magic string
    const char* magic = "PERUN_HELLO";
    for (int i = 0; i < 11; ++i) {
        hello.push_back(magic[i]);
    }
    
    // Version (big-endian)
    hello.push_back((version >> 8) & 0xFF);
    hello.push_back(version & 0xFF);
    
    // Capabilities (big-endian)
    hello.push_back((caps >> 8) & 0xFF);
    hello.push_back(caps & 0xFF);
    
    return hello;
}

HandshakeResult Handshake::ProcessHello(const uint8_t* data, size_t len, uint16_t serverCaps) {
    HandshakeResult result;
    result.accepted = false;
    result.version = 0;
    result.capabilities = 0;
    
    // Check minimum length
    if (len < 15) {
        result.error = "Handshake too short";
        return result;
    }
    
    // Verify magic string
    if (memcmp(data, "PERUN_HELLO", 11) != 0) {
        result.error = "Invalid magic string";
        return result;
    }
    
    // Read version
    uint16_t clientVersion = (data[11] << 8) | data[12];
    
    // Check version compatibility
    if (clientVersion != PROTOCOL_VERSION) {
        result.error = "Unsupported protocol version";
        result.version = clientVersion;
        return result;
    }
    
    // Read client capabilities
    uint16_t clientCaps = (data[13] << 8) | data[14];
    
    // Negotiate capabilities (intersection of client and server)
    uint16_t negotiated = clientCaps & serverCaps;
    
    result.accepted = true;
    result.version = PROTOCOL_VERSION;
    result.capabilities = negotiated;
    
    return result;
}

std::vector<uint8_t> Handshake::CreateOk(uint16_t version, uint16_t caps) {
    std::vector<uint8_t> ok;
    ok.reserve(6);
    
    // Magic string
    ok.push_back('O');
    ok.push_back('K');
    
    // Version (big-endian)
    ok.push_back((version >> 8) & 0xFF);
    ok.push_back(version & 0xFF);
    
    // Negotiated capabilities (big-endian)
    ok.push_back((caps >> 8) & 0xFF);
    ok.push_back(caps & 0xFF);
    
    return ok;
}

std::vector<uint8_t> Handshake::CreateError(const std::string& msg) {
    std::vector<uint8_t> error;
    error.reserve(5 + msg.size() + 1);
    
    // Magic string
    const char* magic = "ERROR";
    for (int i = 0; i < 5; ++i) {
        error.push_back(magic[i]);
    }
    
    // Error message (null-terminated)
    for (char c : msg) {
        error.push_back(static_cast<uint8_t>(c));
    }
    error.push_back(0);  // Null terminator
    
    return error;
}

} // namespace Perun::Protocol
