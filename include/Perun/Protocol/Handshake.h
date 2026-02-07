#pragma once

#include <vector>
#include <cstdint>
#include <string>

namespace Perun::Protocol {

constexpr uint16_t PROTOCOL_VERSION = 1;

enum Capabilities : uint16_t {
    CAP_DELTA = 0x01,   // Supports delta frames
    CAP_AUDIO = 0x02,   // Supports audio streaming
    CAP_DEBUG = 0x04,   // Supports debug info packets
};

struct HandshakeResult {
    bool accepted;
    uint16_t version;
    uint16_t capabilities;  // Negotiated capabilities
    std::string error;
};

class Handshake {
public:
    static std::vector<uint8_t> CreateHello(uint16_t version, uint16_t caps);
    static HandshakeResult ProcessHello(const uint8_t* data, size_t len, uint16_t serverCaps = CAP_DELTA | CAP_AUDIO | CAP_DEBUG);
    static std::vector<uint8_t> CreateOk(uint16_t version, uint16_t caps);
    static std::vector<uint8_t> CreateError(const std::string& msg);
};

} // namespace Perun::Protocol
