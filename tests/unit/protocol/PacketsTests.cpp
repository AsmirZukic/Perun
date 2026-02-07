#include <gtest/gtest.h>
#include "Perun/Protocol/Packets.h"
#include <cstring>

using namespace Perun::Protocol;

TEST(Protocol, PacketHeaderSize) {
    // Type(1) + Flags(1) + Seq(2) + Length(4) = 8 bytes
    EXPECT_EQ(sizeof(PacketHeader), 8);
}

TEST(Protocol, PacketHeaderSerialize) {
    PacketHeader header;
    header.type = PacketType::VideoFrame;
    header.flags = 0x00;
    header.sequence = 0x1234;
    header.length = 0x5678;
    
    auto bytes = header.Serialize();
    
    ASSERT_EQ(bytes.size(), 8);
    EXPECT_EQ(bytes[0], 0x01);  // Type
    EXPECT_EQ(bytes[1], 0x00);  // Flags
    // Sequence is big-endian uint16_t
    EXPECT_EQ(bytes[2], 0x12);
    EXPECT_EQ(bytes[3], 0x34);
    // Length is big-endian uint32_t
    EXPECT_EQ(bytes[4], 0x00);
    EXPECT_EQ(bytes[5], 0x00);
    EXPECT_EQ(bytes[6], 0x56);
    EXPECT_EQ(bytes[7], 0x78);
}

TEST(Protocol, PacketHeaderDeserialize) {
    uint8_t data[] = {0x01, 0x00, 0x12, 0x34, 0x00, 0x00, 0x56, 0x78};
    auto header = PacketHeader::Deserialize(data);
    
    EXPECT_EQ(header.type, PacketType::VideoFrame);
    EXPECT_EQ(header.flags, 0x00);
    EXPECT_EQ(header.sequence, 0x1234);
    EXPECT_EQ(header.length, 0x5678);
}

TEST(Protocol, VideoFramePacketSerialize) {
    VideoFramePacket packet;
    packet.width = 64;
    packet.height = 32;
    packet.isDelta = false;
    packet.compressedData = {0xAA, 0xBB, 0xCC};
    
    auto bytes = packet.Serialize();
    
    // Width(2) + Height(2) + Data(3) = 7 bytes
    ASSERT_EQ(bytes.size(), 7);
    EXPECT_EQ(bytes[0], 0x00);  // Width high byte
    EXPECT_EQ(bytes[1], 0x40);  // Width low byte (64)
    EXPECT_EQ(bytes[2], 0x00);  // Height high byte
    EXPECT_EQ(bytes[3], 0x20);  // Height low byte (32)
    EXPECT_EQ(bytes[4], 0xAA);
    EXPECT_EQ(bytes[5], 0xBB);
    EXPECT_EQ(bytes[6], 0xCC);
}

TEST(Protocol, VideoFramePacketDeserialize) {
    uint8_t data[] = {0x00, 0x40, 0x00, 0x20, 0xAA, 0xBB, 0xCC};
    auto packet = VideoFramePacket::Deserialize(data, 7);
    
    EXPECT_EQ(packet.width, 64);
    EXPECT_EQ(packet.height, 32);
    ASSERT_EQ(packet.compressedData.size(), 3);
    EXPECT_EQ(packet.compressedData[0], 0xAA);
    EXPECT_EQ(packet.compressedData[1], 0xBB);
    EXPECT_EQ(packet.compressedData[2], 0xCC);
}

TEST(Protocol, InputEventPacketSerialize) {
    InputEventPacket packet;
    packet.buttons = 0xABCD;
    packet.reserved = 0;
    
    auto bytes = packet.Serialize();
    
    ASSERT_EQ(bytes.size(), 4);
    EXPECT_EQ(bytes[0], 0xAB);
    EXPECT_EQ(bytes[1], 0xCD);
    EXPECT_EQ(bytes[2], 0x00);
    EXPECT_EQ(bytes[3], 0x00);
}

TEST(Protocol, InputEventPacketDeserialize) {
    uint8_t data[] = {0xAB, 0xCD, 0x00, 0x00};
    auto packet = InputEventPacket::Deserialize(data, 4);
    
    EXPECT_EQ(packet.buttons, 0xABCD);
    EXPECT_EQ(packet.reserved, 0);
}

TEST(Protocol, AudioChunkPacketSerialize) {
    AudioChunkPacket packet;
    packet.sampleRate = 44100;
    packet.channels = 2;
    packet.samples = {100, -200, 300};
    
    auto bytes = packet.Serialize();
    
    // SampleRate(2) + Channels(1) + Samples(3*2=6) = 9 bytes
    ASSERT_EQ(bytes.size(), 9);
    
    // Sample rate in big-endian
    uint16_t sr = (bytes[0] << 8) | bytes[1];
    EXPECT_EQ(sr, 44100);
    EXPECT_EQ(bytes[2], 2);  // Channels
    
    // Samples in big-endian int16_t
    int16_t s1 = (bytes[3] << 8) | bytes[4];
    int16_t s2 = (bytes[5] << 8) | bytes[6];
    int16_t s3 = (bytes[7] << 8) | bytes[8];
    EXPECT_EQ(s1, 100);
    EXPECT_EQ(s2, -200);
    EXPECT_EQ(s3, 300);
}

TEST(Protocol, ComputeDelta) {
    uint8_t frame1[] = {0x00, 0xFF, 0x00, 0xFF};
    uint8_t frame2[] = {0x00, 0xFF, 0xFF, 0x00};
    
    auto delta = VideoFramePacket::ComputeDelta(frame2, frame1, 4);
    
    ASSERT_EQ(delta.size(), 4);
    EXPECT_EQ(delta[0], 0x00);  // 0x00 ^ 0x00
    EXPECT_EQ(delta[1], 0x00);  // 0xFF ^ 0xFF
    EXPECT_EQ(delta[2], 0xFF);  // 0xFF ^ 0x00
    EXPECT_EQ(delta[3], 0xFF);  // 0x00 ^ 0xFF
}

TEST(Protocol, ApplyDelta) {
    uint8_t frame1[] = {0x00, 0xFF, 0x00, 0xFF};
    uint8_t delta[] = {0x00, 0x00, 0xFF, 0xFF};
    uint8_t output[4];
    
    memcpy(output, frame1, 4);
    VideoFramePacket::ApplyDelta(output, delta, 4);
    
    EXPECT_EQ(output[0], 0x00);
    EXPECT_EQ(output[1], 0xFF);
    EXPECT_EQ(output[2], 0xFF);
    EXPECT_EQ(output[3], 0x00);
}
