#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <array>

namespace Perun {
namespace Transport {
namespace Crypto {

// Minimal SHA1 implementation for WebSocket handshake
class SHA1 {
public:
    SHA1() { reset(); }

    void update(const std::string& s) {
        update(s.c_str(), s.length());
    }

    void update(const void* data, size_t len) {
        const uint8_t* d = (const uint8_t*)data;
        for (size_t i = 0; i < len; ++i) {
            m_buffer[m_bufferIndex++] = d[i];
            if (m_bufferIndex == 64) {
                processBlock();
                m_bufferIndex = 0;
            }
        }
        m_count += len * 8;
    }

    std::string final() {
        uint8_t finalCount[8];
        for (int i = 0; i < 8; ++i) {
            finalCount[i] = (uint8_t)((m_count >> ((7 - i) * 8)) & 255);
        }

        update("\200", 1);
        while (m_bufferIndex != 56) {
            update("\0", 1);
        }

        update(finalCount, 8); // Should trigger processBlock

        std::string digest;
        digest.resize(20);
        for (int i = 0; i < 5; ++i) {
            digest[i * 4] = (uint8_t)((m_state[i] >> 24) & 255);
            digest[i * 4 + 1] = (uint8_t)((m_state[i] >> 16) & 255);
            digest[i * 4 + 2] = (uint8_t)((m_state[i] >> 8) & 255);
            digest[i * 4 + 3] = (uint8_t)((m_state[i]) & 255);
        }
        return digest;
    }

private:
    void reset() {
        m_state[0] = 0x67452301;
        m_state[1] = 0xEFCDAB89;
        m_state[2] = 0x98BADCFE;
        m_state[3] = 0x10325476;
        m_state[4] = 0xC3D2E1F0;
        m_count = 0;
        m_bufferIndex = 0;
    }

    uint32_t leftRotate(uint32_t value, size_t count) {
        return (value << count) ^ (value >> (32 - count));
    }

    void processBlock() {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (m_buffer[i * 4] << 24) | (m_buffer[i * 4 + 1] << 16) |
                   (m_buffer[i * 4 + 2] << 8) | (m_buffer[i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = leftRotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = m_state[0];
        uint32_t b = m_state[1];
        uint32_t c = m_state[2];
        uint32_t d = m_state[3];
        uint32_t e = m_state[4];

        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = leftRotate(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = leftRotate(b, 30);
            b = a;
            a = temp;
        }

        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
    }

    uint32_t m_state[5];
    uint64_t m_count;
    uint8_t m_buffer[64];
    size_t m_bufferIndex;
};

// Base64 encoding
inline std::string Base64Encode(const std::string& input) {
    static const char* kBase64Chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string output;
    output.reserve(((input.length() / 3) + (input.length() % 3 > 0)) * 4);
    
    uint32_t val = 0;
    int valb = -6;
    for (uint8_t c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            output.push_back(kBase64Chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    
    if (valb > -6) {
        output.push_back(kBase64Chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    
    while (output.size() % 4) {
        output.push_back('=');
    }
    
    return output;
}

} // namespace Crypto
} // namespace Transport
} // namespace Perun
