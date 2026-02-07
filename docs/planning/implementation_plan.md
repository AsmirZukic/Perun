# Perun Implementation Plan

Step-by-step guide to transform Perun into the Universal Emulator Frontend Platform.

---

## Phase 0: Repository Restructure (Day 1)

### 0.1 Create New Directory Structure
```bash
mkdir -p include/Perun/{Protocol,Transport}
mkdir -p src/{Protocol,Transport}
mkdir -p sdk/{python,c}
mkdir -p tests/{unit/protocol,unit/transport,integration,conformance}
mkdir -p web
mkdir -p docs/planning
```

### 0.2 Remove/Keep Components
| Remove | Keep |
|--------|------|
| `include/Perun/Math/` | `src/Core/Window.cpp` |
| `src/Math/` | `src/Graphics/Texture.cpp` |
| `examples/main.cpp` | `src/Graphics/Renderer.cpp` (simplify) |
|  | `src/Audio/` (will refactor) |
|  | `src/Server/main.cpp` (will refactor) |
|  | `src/ImGui/` ✨ **Keep for debug overlay** |
|  | `include/Perun/ImGui/` ✨ **Debug UI** |

### 0.3 Update CMakeLists.txt
- Keep ImGui fetch (for debug overlay)
- Remove Math library sources
- Add Protocol/Transport targets
- Add test targets for new structure

### 0.4 Commit
```bash
git checkout -b phase-0-restructure
git add -A
git commit -m "refactor: restructure for emulator platform"
```

---

## Phase 1: Protocol Layer (Day 2-3)

> **TDD Approach**: Write tests first, then implement.

### 1.1 Define Packet Structures

**Test First:**
```cpp
// tests/unit/protocol/test_packets.cpp
TEST(Protocol, PacketHeaderSize) {
    // Type(1) + Flags(1) + Seq(2) + Length(4) = 8 bytes
    EXPECT_EQ(sizeof(PacketHeader), 8);
}

TEST(Protocol, SerializeVideoFrame) {
    VideoFramePacket packet{.width = 64, .height = 32, .data = {...}};
    auto bytes = packet.Serialize();
    EXPECT_EQ(bytes[0], 0x01);  // Type
    EXPECT_EQ(bytes[1] & 0x01, 0);  // Full frame (not delta)
}

TEST(Protocol, SerializeDeltaFrame) {
    VideoFramePacket packet{.isDelta = true, .data = {...}};
    auto bytes = packet.Serialize();
    EXPECT_EQ(bytes[1] & 0x01, 1);  // Delta flag set
}
```

**Then Implement:**
```cpp
// include/Perun/Protocol/Packets.h
#pragma once
#include <cstdint>
#include <vector>

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
```

### 1.2 Implement Handshake with Capabilities

**Test First:**
```cpp
// tests/unit/protocol/test_handshake.cpp
TEST(Handshake, ValidClientHello) {
    auto hello = Handshake::CreateHello(PROTOCOL_VERSION, CAP_DELTA | CAP_AUDIO);
    EXPECT_EQ(memcmp(hello.data(), "PERUN_HELLO", 11), 0);
    // Version at offset 11, capabilities at 13
}

TEST(Handshake, CapabilityNegotiation) {
    // Client supports delta, server doesn't
    auto result = Handshake::ProcessHello(v1, CAP_DELTA);
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.capabilities & CAP_DELTA, 0);  // Negotiated out
}
```

**Then Implement:**
```cpp
// include/Perun/Protocol/Handshake.h
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
    static HandshakeResult ProcessHello(const uint8_t* data, size_t len);
    static std::vector<uint8_t> CreateOk(uint16_t version, uint16_t caps);
    static std::vector<uint8_t> CreateError(const std::string& msg);
};

}
```

### 1.3 Add LZ4 + Delta Compression

**CMakeLists.txt:**
```cmake
find_package(lz4 REQUIRED)
target_link_libraries(PerunProtocol PUBLIC lz4::lz4)
```

**Tests:**
```cpp
TEST(Compression, LZ4RoundTrip) {
    std::vector<uint8_t> original(640 * 480 * 4, 0xAB);
    auto compressed = Compress(original);
    auto decompressed = Decompress(compressed, original.size());
    EXPECT_EQ(original, decompressed);
}

TEST(Compression, DeltaFrame) {
    // Two similar frames
    std::vector<uint8_t> frame1(1024, 0x00);
    std::vector<uint8_t> frame2(1024, 0x00);
    frame2[512] = 0xFF;  // One pixel different
    
    auto delta = VideoFramePacket::ComputeDelta(
        frame2.data(), frame1.data(), 1024);
    auto deltaCompressed = Compress(delta);
    auto fullCompressed = Compress(frame2);
    
    // Delta should be much smaller
    EXPECT_LT(deltaCompressed.size(), fullCompressed.size() / 2);
}
```

**Implementation:**
```cpp
// src/Protocol/Compression.cpp
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
```

### 1.4 Commit
```bash
git add -A
git commit -m "feat(protocol): implement packet serialization and handshake"
```

---

## Phase 2: Transport Layer (Day 4-6)

### 2.1 Define Transport Interface

```cpp
// include/Perun/Transport/ITransport.h
#pragma once
#include <vector>
#include <functional>
#include <memory>

namespace Perun::Transport {

class IConnection {
public:
    virtual ~IConnection() = default;
    virtual bool Send(const uint8_t* data, size_t len) = 0;
    virtual size_t Recv(uint8_t* buf, size_t maxLen) = 0;
    virtual void Close() = 0;
    virtual bool IsConnected() const = 0;
};

class ITransport {
public:
    virtual ~ITransport() = default;
    virtual bool Listen(const std::string& address) = 0;
    virtual std::unique_ptr<IConnection> Accept() = 0;
    virtual std::unique_ptr<IConnection> Connect(const std::string& address) = 0;
    virtual void Close() = 0;
};

// Factory
std::unique_ptr<ITransport> CreateUnixTransport();
std::unique_ptr<ITransport> CreateTcpTransport();
std::unique_ptr<ITransport> CreateWebSocketTransport();

}
```

### 2.2 Implement Unix Socket Transport (Refactor Existing)

**Test:**
```cpp
TEST(UnixTransport, ConnectAndSend) {
    auto server = CreateUnixTransport();
    server->Listen("/tmp/test.sock");
    
    std::thread clientThread([&] {
        auto client = CreateUnixTransport();
        auto conn = client->Connect("/tmp/test.sock");
        conn->Send((uint8_t*)"hello", 5);
    });
    
    auto conn = server->Accept();
    uint8_t buf[10];
    size_t n = conn->Recv(buf, 10);
    EXPECT_EQ(n, 5);
    EXPECT_EQ(memcmp(buf, "hello", 5), 0);
    
    clientThread.join();
}
```

**Implement:** Refactor `src/Server/main.cpp` socket code into `src/Transport/UnixTransport.cpp`.

### 2.3 Implement TCP Transport

**Test:**
```cpp
TEST(TcpTransport, ConnectOverNetwork) {
    auto server = CreateTcpTransport();
    server->Listen("0.0.0.0:9000");
    
    std::thread clientThread([&] {
        auto client = CreateTcpTransport();
        auto conn = client->Connect("127.0.0.1:9000");
        conn->Send((uint8_t*)"hello", 5);
    });
    
    auto conn = server->Accept();
    // ... same as above
}
```

**Implement:** `src/Transport/TcpTransport.cpp` using standard BSD sockets.

**Critical: Enable TCP_NODELAY:**
```cpp
// In TcpConnection constructor
int one = 1;
setsockopt(m_Socket, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
```

### 2.4 Commit
```bash
git add -A
git commit -m "feat(transport): add transport abstraction with Unix/TCP implementations"
```

---

## Phase 3: Refactor Server (Day 7-9)

### 3.1 Create Server Class

```cpp
// include/Perun/Server/Server.h
#pragma once
#include "Perun/Transport/ITransport.h"
#include "Perun/Protocol/Packets.h"
#include <vector>
#include <memory>

namespace Perun::Server {

struct ClientConnection {
    std::unique_ptr<Transport::IConnection> transport;
    uint64_t sessionId;
    bool handshakeComplete;
};

class Server {
public:
    struct Config {
        std::string listenAddress;  // "unix:///tmp/perun.sock" or "tcp://0.0.0.0:9000"
        int maxClients = 6;
    };
    
    explicit Server(const Config& config);
    
    bool Start();
    void Stop();
    void Run();  // Main loop
    
    // Callbacks for emulator integration
    using FrameCallback = std::function<void(uint64_t sessionId, const Protocol::VideoFramePacket&)>;
    using InputCallback = std::function<void(uint64_t sessionId, const Protocol::InputEventPacket&)>;
    
    void SetFrameCallback(FrameCallback cb);
    void SetInputCallback(InputCallback cb);
    
    void BroadcastFrame(const Protocol::VideoFramePacket& frame);
    void SendInput(uint64_t sessionId, const Protocol::InputEventPacket& input);

private:
    Config m_Config;
    std::unique_ptr<Transport::ITransport> m_Transport;
    std::vector<ClientConnection> m_Clients;
};

}
```

### 3.2 Refactor main.cpp

**Before:** Monolithic 200-line main.cpp with inline socket/SHM code  
**After:** Clean main that uses Server class

```cpp
// src/Server/main.cpp
#include "Perun/Server/Server.h"
#include <iostream>

int main(int argc, char** argv) {
    Perun::Server::Config config;
    config.listenAddress = "tcp://0.0.0.0:9000";
    
    // Parse args for --unix, --port, etc.
    
    Perun::Server::Server server(config);
    
    server.SetFrameCallback([](auto sessionId, const auto& frame) {
        std::cout << "Received frame " << frame.frameId << std::endl;
    });
    
    if (!server.Start()) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    std::cout << "Perun Server running on " << config.listenAddress << std::endl;
    server.Run();
    
    return 0;
}
```

### 3.3 Commit
```bash
git add -A
git commit -m "refactor(server): extract Server class, support multiple transports"
```

---

## Phase 4: Python SDK (Day 10-11)

### 4.1 Create SDK Package

```
sdk/python/
├── pyproject.toml
├── perun_sdk/
│   ├── __init__.py
│   ├── connection.py
│   ├── protocol.py
│   └── core.py
└── tests/
    └── test_protocol.py
```

### 4.2 Implement Protocol in Python

```python
# sdk/python/perun_sdk/protocol.py
import struct
from enum import IntEnum, IntFlag
from dataclasses import dataclass
import lz4.frame

class PacketType(IntEnum):
    VIDEO_FRAME = 0x01
    AUDIO_CHUNK = 0x02
    INPUT_EVENT = 0x03
    CONFIG = 0x04
    DEBUG_INFO = 0x05

class PacketFlags(IntFlag):
    DELTA = 0x01
    COMPRESS_FAST = 0x02
    COMPRESS_DEFAULT = 0x04

class Capabilities(IntFlag):
    DELTA = 0x01
    AUDIO = 0x02
    DEBUG = 0x04

@dataclass
class VideoFramePacket:
    width: int
    height: int
    data: bytes
    is_delta: bool = False
    sequence: int = 0
    
    def serialize(self) -> bytes:
        compressed = lz4.frame.compress(self.data)
        flags = PacketFlags.DELTA if self.is_delta else 0
        # Header: Type(1) + Flags(1) + Seq(2) + Length(4)
        header = struct.pack('>BBHI', 
            PacketType.VIDEO_FRAME, 
            flags,
            self.sequence,
            len(compressed) + 4)  # +4 for width/height
        payload = struct.pack('>HH', self.width, self.height) + compressed
        return header + payload
    
    @classmethod
    def deserialize(cls, header_flags: int, seq: int, data: bytes) -> 'VideoFramePacket':
        width, height = struct.unpack('>HH', data[:4])
        compressed = data[4:]
        is_delta = bool(header_flags & PacketFlags.DELTA)
        return cls(width, height, lz4.frame.decompress(compressed), is_delta, seq)

@dataclass
class InputEventPacket:
    buttons: int  # Bitmask
    sequence: int = 0
    
    def serialize(self) -> bytes:
        header = struct.pack('>BBHI',
            PacketType.INPUT_EVENT,
            0,  # flags
            self.sequence,
            4)  # payload: buttons(2) + reserved(2)
        payload = struct.pack('>HH', self.buttons, 0)
        return header + payload
```

### 4.3 Implement Connection with Latest-Wins Input

```python
# sdk/python/perun_sdk/connection.py
import socket
import struct
import time
from .protocol import PacketType, VideoFramePacket, InputEventPacket, Capabilities
from .core import PerunCore

PROTOCOL_VERSION = 1

class PerunConnection:
    def __init__(self, address: str):
        self.address = address
        self.sock = None
        self.sequence = 0
        self.capabilities = 0
        self.last_frame = None  # For delta compression
    
    def connect(self):
        if self.address.startswith("unix://"):
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.connect(self.address[7:])
        elif self.address.startswith("tcp://"):
            host, port = self.address[6:].split(":")
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.sock.connect((host, int(port)))
        
        self._handshake()
    
    def _handshake(self):
        caps = Capabilities.DELTA | Capabilities.AUDIO
        hello = b"PERUN_HELLO" + struct.pack(">HH", PROTOCOL_VERSION, caps)
        self.sock.send(hello)
        response = self.sock.recv(16)
        if response[:2] != b"OK":
            raise ConnectionError(f"Handshake failed: {response}")
        # Parse negotiated capabilities
        self.capabilities = struct.unpack(">H", response[4:6])[0]
    
    def run(self, core: PerunCore):
        """Main loop - runs the emulator."""
        self.sock.setblocking(False)
        target_fps = core.get_config().get('fps', 60)
        frame_time = 1.0 / target_fps
        
        while True:
            start = time.perf_counter()
            
            # Get input (latest-wins: use highest sequence)
            input_state = self._poll_input()
            
            # Tick emulator
            vram, audio = core.tick(input_state)
            
            # Send frame (with delta if supported)
            self._send_frame(vram, core.get_config())
            
            # Send audio
            if audio:
                self._send_audio(audio)
            
            # Frame pacing
            elapsed = time.perf_counter() - start
            if elapsed < frame_time:
                time.sleep(frame_time - elapsed)
    
    def _send_frame(self, vram: bytes, config: dict):
        is_delta = False
        data = vram
        
        # Use delta compression if supported and beneficial
        if self.capabilities & Capabilities.DELTA and self.last_frame:
            delta = bytes(a ^ b for a, b in zip(vram, self.last_frame))
            if len(lz4.frame.compress(delta)) < len(lz4.frame.compress(vram)) * 0.7:
                data = delta
                is_delta = True
        
        packet = VideoFramePacket(
            width=config['width'],
            height=config['height'],
            data=data,
            is_delta=is_delta,
            sequence=self.sequence
        )
        self.sock.send(packet.serialize())
        self.sequence += 1
        self.last_frame = vram
```

### 4.4 Core Interface

```python
# sdk/python/perun_sdk/core.py
from abc import ABC, abstractmethod

class PerunCore(ABC):
    @abstractmethod
    def tick(self, input_state: int) -> tuple[bytes, bytes]:
        """Run one frame. Return (vram, audio)."""
        pass
    
    @abstractmethod
    def get_config(self) -> dict:
        """Return emulator config: width, height, fps, etc."""
        pass
    
    def load_state(self, state: bytes):
        """Load save state. Override if supported."""
        pass
    
    def save_state(self) -> bytes:
        """Save state. Override if supported."""
        return b""
```

### 4.5 Commit
```bash
git add -A
git commit -m "feat(sdk): add Python SDK for emulator cores"
```

---

## Phase 5: Test Infrastructure (Day 12-14)

### 5.1 Mock Emulator Core

```python
# tests/integration/mock_core.py
from perun_sdk import PerunCore, PerunConnection
import struct

class MockCore(PerunCore):
    """Generates test patterns for integration testing."""
    
    def __init__(self, width=64, height=32):
        self.width = width
        self.height = height
        self.frame = 0
    
    def tick(self, input_state: int) -> tuple[bytes, bytes]:
        # Generate checkerboard pattern
        vram = bytearray(self.width * self.height * 4)
        for y in range(self.height):
            for x in range(self.width):
                idx = (y * self.width + x) * 4
                val = 255 if (x + y + self.frame) % 2 else 0
                vram[idx:idx+4] = [val, val, val, 255]
        
        self.frame += 1
        return bytes(vram), b""
    
    def get_config(self) -> dict:
        return {"width": self.width, "height": self.height, "fps": 60}

if __name__ == "__main__":
    import sys
    address = sys.argv[1] if len(sys.argv) > 1 else "tcp://localhost:9000"
    
    core = MockCore()
    conn = PerunConnection(address)
    conn.connect()
    conn.run(core)
```

### 5.2 Test Client (C++)

```cpp
// tests/integration/test_client.cpp
#include <gtest/gtest.h>
#include "Perun/Transport/ITransport.h"
#include "Perun/Protocol/Packets.h"
#include "Perun/Protocol/Handshake.h"

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Start server in background
        serverThread = std::thread([] {
            system("./perun-server --port 9999 &");
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    void TearDown() override {
        system("pkill -f 'perun-server --port 9999'");
        if (serverThread.joinable()) serverThread.join();
    }
    
    std::thread serverThread;
};

TEST_F(IntegrationTest, HandshakeSucceeds) {
    auto transport = Perun::Transport::CreateTcpTransport();
    auto conn = transport->Connect("127.0.0.1:9999");
    ASSERT_NE(conn, nullptr);
    
    auto hello = Perun::Protocol::Handshake::CreateHello(1);
    conn->Send(hello.data(), hello.size());
    
    uint8_t response[16];
    size_t n = conn->Recv(response, 16);
    EXPECT_GT(n, 0);
    EXPECT_EQ(memcmp(response, "OK", 2), 0);
}

TEST_F(IntegrationTest, FrameRoundtrip) {
    // Connect mock core
    // Connect test client
    // Verify frame received matches sent
}
```

### 5.3 Conformance Test Suite

```python
# tests/conformance/protocol_test.py
"""
Test that a core implementation conforms to the Perun protocol.
Run against any core: python protocol_test.py tcp://localhost:9000
"""
import socket
import struct
import sys

def test_handshake(address: str) -> bool:
    sock = connect(address)
    sock.send(b"PERUN_HELLO" + struct.pack(">H", 1))
    response = sock.recv(16)
    return response.startswith(b"OK")

def test_receives_frame(address: str) -> bool:
    # Connect as client, wait for frame packet
    sock = connect(address)
    do_handshake(sock)
    
    header = sock.recv(5)
    ptype, length = struct.unpack(">BI", header)
    return ptype == 0x01 and length > 0

def test_input_delivery(address: str) -> bool:
    # Send input, verify core responds
    pass

if __name__ == "__main__":
    address = sys.argv[1]
    
    tests = [
        ("Handshake", test_handshake),
        ("Frame Reception", test_receives_frame),
        ("Input Delivery", test_input_delivery),
    ]
    
    for name, test in tests:
        try:
            result = test(address)
            status = "✓" if result else "✗"
        except Exception as e:
            status = f"✗ ({e})"
        print(f"{status} {name}")
```

### 5.4 Commit
```bash
git add -A
git commit -m "test: add mock core, integration tests, and conformance suite"
```

---

## Phase 6: Native Client Refactor (Day 15-17)

### 6.1 Simplify Renderer

Keep only what's needed for frame display:

```cpp
// include/Perun/Client/Renderer.h
#pragma once

namespace Perun::Client {

class Renderer {
public:
    bool Init(int width, int height);
    void Shutdown();
    
    void UploadFrame(const uint8_t* rgbaData, int width, int height);
    void Present();
    
private:
    unsigned int m_TextureId = 0;
    unsigned int m_VaoId = 0;
    unsigned int m_ShaderId = 0;
};

}
```

### 6.2 Create Client Application

```cpp
// src/Client/main.cpp
#include "Perun/Client/Renderer.h"
#include "Perun/Client/AudioPlayer.h"
#include "Perun/Transport/ITransport.h"
#include "Perun/Protocol/Packets.h"
#include <SDL2/SDL.h>

int main(int argc, char** argv) {
    std::string serverAddress = "tcp://localhost:9000";
    // Parse args...
    
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    SDL_Window* window = SDL_CreateWindow("Perun", ...);
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    
    Perun::Client::Renderer renderer;
    renderer.Init(640, 480);
    
    auto transport = Perun::Transport::CreateTcpTransport();
    auto conn = transport->Connect(serverAddress);
    // Handshake...
    
    bool running = true;
    while (running) {
        // Poll SDL events → send as InputEventPacket
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                // Send input to server
            }
        }
        
        // Receive frames from server
        auto packet = ReceivePacket(conn);
        if (packet.type == PacketType::VideoFrame) {
            auto frame = VideoFramePacket::Deserialize(packet.data);
            renderer.UploadFrame(frame.data.data(), 640, 480);
        }
        
        renderer.Present();
        SDL_GL_SwapWindow(window);
    }
    
    return 0;
}
```

### 6.3 Commit
```bash
git add -A
git commit -m "feat(client): create standalone native client application"
```

---

## Summary Timeline

| Phase | Days | Deliverable |
|-------|------|-------------|
| 0: Restructure | 1 | Clean repo structure |
| 1: Protocol | 2 | Packet serialization, handshake, LZ4 |
| 2: Transport | 3 | ITransport, Unix, TCP implementations |
| 3: Server | 3 | Server class, multi-transport support |
| 4: Python SDK | 2 | perun-sdk package |
| 5: Testing | 3 | Mock core, integration tests, conformance |
| 6: Client | 3 | Standalone native client |
| **Total** | **17 days** | **Network play working** |

---

## Next: Phase 7-8 (Future)

- **Phase 7: WASM Client** - Emscripten build, WebSocket transport
- **Phase 8: Browser UI** - HTML/JS shell, session management
- **Phase 9: Production** - Docker, deployment, monitoring
