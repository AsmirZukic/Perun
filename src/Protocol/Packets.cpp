#include "Perun/Protocol/Packets.h"
#include <cstring>
#include <arpa/inet.h>  // For htons, htonl, ntohs, ntohl

namespace Perun::Protocol {

// ========== PacketHeader ==========

std::vector<uint8_t> PacketHeader::Serialize() const {
    std::vector<uint8_t> bytes(8);
    
    bytes[0] = static_cast<uint8_t>(type);
    bytes[1] = flags;
    
    // Network byte order (big-endian) for sequence
    uint16_t seq_be = htons(sequence);
    memcpy(&bytes[2], &seq_be, 2);
    
    // Network byte order (big-endian) for length
    uint32_t len_be = htonl(length);
    memcpy(&bytes[4], &len_be, 4);
    
    return bytes;
}

PacketHeader PacketHeader::Deserialize(const uint8_t* data) {
    PacketHeader header;
    
    header.type = static_cast<PacketType>(data[0]);
    header.flags = data[1];
    
    // Convert from network byte order
    uint16_t seq_be;
    memcpy(&seq_be, &data[2], 2);
    header.sequence = ntohs(seq_be);
    
    uint32_t len_be;
    memcpy(&len_be, &data[4], 4);
    header.length = ntohl(len_be);
    
    return header;
}

// ========== VideoFramePacket ==========

std::vector<uint8_t> VideoFramePacket::Serialize() const {
    std::vector<uint8_t> bytes;
    bytes.reserve(4 + compressedData.size());
    
    // Width and height in big-endian (network byte order)
    bytes.push_back((width >> 8) & 0xFF);
    bytes.push_back(width & 0xFF);
    bytes.push_back((height >> 8) & 0xFF);
    bytes.push_back(height & 0xFF);
    
    // Append compressed data
    bytes.insert(bytes.end(), compressedData.begin(), compressedData.end());
    
    return bytes;
}

VideoFramePacket VideoFramePacket::Deserialize(const uint8_t* data, size_t len) {
    VideoFramePacket packet;
    
    if (len < 4) {
        return packet;  // Invalid
    }
    
    // Read width and height (already in big-endian from network)
    packet.width = (data[0] << 8) | data[1];
    packet.height = (data[2] << 8) | data[3];
    
    // Read compressed data
    packet.compressedData.assign(data + 4, data + len);
    
    return packet;
}

std::vector<uint8_t> VideoFramePacket::ComputeDelta(
    const uint8_t* current, const uint8_t* previous, size_t size) {
    std::vector<uint8_t> delta(size);
    for (size_t i = 0; i < size; ++i) {
        delta[i] = current[i] ^ previous[i];
    }
    return delta;
}

void VideoFramePacket::ApplyDelta(
    uint8_t* output, const uint8_t* delta, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        output[i] ^= delta[i];
    }
}

// ========== InputEventPacket ==========

std::vector<uint8_t> InputEventPacket::Serialize() const {
    std::vector<uint8_t> bytes(4);
    
    // Big-endian byte order
    bytes[0] = (buttons >> 8) & 0xFF;
    bytes[1] = buttons & 0xFF;
    bytes[2] = (reserved >> 8) & 0xFF;
    bytes[3] = reserved & 0xFF;
    
    return bytes;
}

InputEventPacket InputEventPacket::Deserialize(const uint8_t* data, size_t len) {
    InputEventPacket packet;
    
    if (len < 4) {
        return packet;
    }
    
    // Read directly as big-endian
    packet.buttons = (data[0] << 8) | data[1];
    packet.reserved = (data[2] << 8) | data[3];
    
    return packet;
}

// ========== AudioChunkPacket ==========

std::vector<uint8_t> AudioChunkPacket::Serialize() const {
    std::vector<uint8_t> bytes;
    bytes.reserve(3 + samples.size() * 2);
    
    // Sample rate in big-endian
    bytes.push_back((sampleRate >> 8) & 0xFF);
    bytes.push_back(sampleRate & 0xFF);
    
    // Channels
    bytes.push_back(channels);
    
    // Samples (each is int16_t in big-endian)
    for (int16_t sample : samples) {
        bytes.push_back((static_cast<uint16_t>(sample) >> 8) & 0xFF);
        bytes.push_back(static_cast<uint16_t>(sample) & 0xFF);
    }
    
    return bytes;
}

AudioChunkPacket AudioChunkPacket::Deserialize(const uint8_t* data, size_t len) {
    AudioChunkPacket packet;
    
    if (len < 3) {
        return packet;
    }
    
    // Read sample rate directly as big-endian
    packet.sampleRate = (data[0] << 8) | data[1];
    
    // Read channels
    packet.channels = data[2];
    
    // Read samples
    size_t numSamples = (len - 3) / 2;
    packet.samples.reserve(numSamples);
    
    for (size_t i = 0; i < numSamples; ++i) {
        size_t offset = 3 + i * 2;
        int16_t sample = static_cast<int16_t>((data[offset] << 8) | data[offset + 1]);
        packet.samples.push_back(sample);
    }
    
    return packet;
}

} // namespace Perun::Protocol
