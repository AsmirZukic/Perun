#include <gtest/gtest.h>
#include "Perun/Protocol/Handshake.h"
#include <cstring>

using namespace Perun::Protocol;

TEST(Handshake, ValidClientHello) {
    auto hello = Handshake::CreateHello(PROTOCOL_VERSION, CAP_DELTA | CAP_AUDIO);
    
    ASSERT_GE(hello.size(), 15);  // "PERUN_HELLO" + version(2) + caps(2)
    EXPECT_EQ(memcmp(hello.data(), "PERUN_HELLO", 11), 0);
    
    // Version at offset 11 (big-endian)
    uint16_t version = (hello[11] << 8) | hello[12];
    EXPECT_EQ(version, 1);
    
    // Capabilities at offset 13 (big-endian)
    uint16_t caps = (hello[13] << 8) | hello[14];
    EXPECT_EQ(caps, CAP_DELTA | CAP_AUDIO);
}

TEST(Handshake, CapabilityNegotiation) {
    // Client supports delta + audio, server supports all
    auto hello = Handshake::CreateHello(1, CAP_DELTA | CAP_AUDIO);
    auto result = Handshake::ProcessHello(hello.data(), hello.size(), CAP_DELTA | CAP_AUDIO | CAP_DEBUG);
    
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.version, 1);
    // Negotiated caps should be intersection: delta + audio
    EXPECT_TRUE(result.capabilities & CAP_DELTA);
    EXPECT_TRUE(result.capabilities & CAP_AUDIO);
    EXPECT_FALSE(result.capabilities & CAP_DEBUG);
}

TEST(Handshake, CapabilityNegotiation_ServerSubset) {
    // Client supports delta + audio + debug, server only supports delta
    auto hello = Handshake::CreateHello(1, CAP_DELTA | CAP_AUDIO | CAP_DEBUG);
    auto result = Handshake::ProcessHello(hello.data(), hello.size(), CAP_DELTA);
    
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.capabilities, CAP_DELTA);  // Only what server supports
}

TEST(Handshake, InvalidMagicString) {
    std::vector<uint8_t> badHello = {'B', 'A', 'D', '_', 'H', 'E', 'L', 'L', 'O', '!', '!', 0, 1, 0, 0};
    auto result = Handshake::ProcessHello(badHello.data(), badHello.size());
    
    EXPECT_FALSE(result.accepted);
    EXPECT_FALSE(result.error.empty());
}

TEST(Handshake, UnsupportedVersion) {
    auto hello = Handshake::CreateHello(99, CAP_DELTA);
    auto result = Handshake::ProcessHello(hello.data(), hello.size());
    
    EXPECT_FALSE(result.accepted);
    EXPECT_FALSE(result.error.empty());
}

TEST(Handshake, CreateOkResponse) {
    auto ok = Handshake::CreateOk(1, CAP_DELTA | CAP_AUDIO);
    
    ASSERT_GE(ok.size(), 6);  // "OK" + version(2) + caps(2)
    EXPECT_EQ(memcmp(ok.data(), "OK", 2), 0);
    
    uint16_t version = (ok[2] << 8) | ok[3];
    EXPECT_EQ(version, 1);
    
    uint16_t caps = (ok[4] << 8) | ok[5];
    EXPECT_EQ(caps, CAP_DELTA | CAP_AUDIO);
}

TEST(Handshake, CreateErrorResponse) {
    auto error = Handshake::CreateError("Invalid version");
    
    ASSERT_GT(error.size(), 2);
    EXPECT_EQ(memcmp(error.data(), "ERROR", 5), 0);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(error.data() + 5)), "Invalid version");
}
