# Perun - Universal Emulator Frontend Platform

**Perun** is a high-performance, network-transparent emulator frontend platform built in C++. It enables emulator cores to run anywhere (locally, in containers, on remote servers) while providing a consistent interface for rendering, input, and audio streaming.

## Features

- **Protocol Layer**: Efficient binary protocol for video frames, audio chunks, and input events
- **Transport Abstraction**: Support for Unix sockets, TCP, and WebSocket transports
- **Delta Compression**: XOR-based delta frames with LZ4 compression for minimal bandwidth
- **Multi-Client Support**: Multiple clients can connect and control the same emulator session
- **Platform SDKs**: Python SDK for easy emulator core integration
- **Native Client**: OpenGL-based client for high-performance local rendering
- **Debug Overlay**: ImGui-based debug interface for monitoring performance

## Architecture

```
┌─────────────┐         ┌──────────────┐         ┌─────────────┐
│  Emulator   │ <─SDK─> │ Perun Server │ <─Net─> │   Clients   │
│    Core     │         │  (Protocol)  │         │ (Renderer)  │
└─────────────┘         └──────────────┘         └─────────────┘
```

- **Emulator Core**: Runs the actual emulation (Python, C++, etc.)
- **Perun Server**: Handles protocol, transport, and client connections
- **Clients**: Render video, play audio, send input (native, web, mobile)

## Prerequisites

- **Compiler**: C++20 compliant compiler (`g++`, `clang++`, or `MSVC`)
- **Tools**: `CMake` 3.14+, `Make` or `Ninja`
- **Libraries**:
    - `SDL2` (Development files)
    - `SDL2_image` (Development files)
    - `OpenGL`
    - `lz4` (for compression)

## Building

```bash
# Clone the repository
git clone <repo-url> Perun
cd Perun

# Configure and Build
cmake -S . -B build
cmake --build build

# Run the Server
./build/perun-server --tcp 0.0.0.0:9000
```

## Testing

Perun uses **GoogleTest** for verification.

```bash
cd build
ctest --output-on-failure
```

## Quick Start

### 1. Start the Perun Server

```bash
# TCP mode (for network access)
./build/perun-server --tcp 0.0.0.0:9000

# Unix socket mode (for local IPC)
./build/perun-server --unix /tmp/perun.sock
```

### 2. Create an Emulator Core (Python)

```python
from perun_sdk import PerunCore, PerunConnection

class MyEmulator(PerunCore):
    def tick(self, input_state: int) -> tuple[bytes, bytes]:
        # Run one frame of emulation
        vram = self.generate_frame()  # RGBA pixel data
        audio = self.generate_audio()  # PCM samples
        return vram, audio
    
    def get_config(self) -> dict:
        return {"width": 256, "height": 240, "fps": 60}

# Connect and run
core = MyEmulator()
conn = PerunConnection("tcp://localhost:9000")
conn.connect()
conn.run(core)
```

### 3. Connect a Client

```bash
# Native client (C++)
./build/perun-client --connect tcp://localhost:9000

# Web client (browser)
# Open http://localhost:8080 in your browser
```

## SDK Documentation

See `sdk/python/README.md` for detailed Python SDK documentation.

## Protocol Specification

See `docs/protocol.md` for the complete protocol specification.

## License
MIT