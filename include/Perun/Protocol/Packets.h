#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace Perun::Protocol {

enum class PacketType : uint8_t {
    VideoFrame = 0x01,
    AudioChunk = 0x02,
    InputEvent = 0x03,
    Config     = 0x04,
    DebugInfo  = 0x05
};

enum PacketFlags : uint8_t {
    FLAG_DELTA      = 0x01,  // Bit 0: XOR delta frame
    FLAG_COMPRESS_1 = 0x02,  // Bit 1-2: Compression level
    FLAG_COMPRESS_2 = 0x04,
};

struct PacketHeader {
    PacketType type;
    uint8_t flags;
    uint16_t sequence;
    uint32_t length;
    
    std::vector<uint8_t> Serialize() const;
    static PacketHeader Deserialize(const uint8_t* data);
} __attribute__((packed));

struct VideoFramePacket {
    uint16_t width;
    uint16_t height;
    bool isDelta = false;
    std::vector<uint8_t> compressedData;
    
    std::vector<uint8_t> Serialize() const;
    static VideoFramePacket Deserialize(const uint8_t* data, size_t len);
    
    // Delta compression helper
    static std::vector<uint8_t> ComputeDelta(
        const uint8_t* current, const uint8_t* previous, size_t size);
    static void ApplyDelta(
        uint8_t* output, const uint8_t* delta, size_t size);
};

struct InputEventPacket {
    uint16_t buttons;    // Bitmask of pressed buttons
    uint16_t reserved;
    
    std::vector<uint8_t> Serialize() const;
    static InputEventPacket Deserialize(const uint8_t* data, size_t len);
};

struct AudioChunkPacket {
    uint16_t sampleRate;
    uint8_t channels;
    std::vector<int16_t> samples;
    
    std::vector<uint8_t> Serialize() const;
    static AudioChunkPacket Deserialize(const uint8_t* data, size_t len);
};

} // namespace Perun::Protocol
