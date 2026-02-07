# Perun Architecture Overview

> **Perun**: Universal Emulator Frontend Platform

---

## What Is Perun?

Perun is a **platform for hosting emulators** that enables:
- Running emulators on a server
- Playing via browser (WebSocket + WASM)
- Playing via network (TCP)
- Playing locally (Unix socket)

**Key innovation:** Send VRAM data, not video streams. 10-20x more efficient for retro systems.

---

## Core Components

```
┌─────────────────────────────────────────────────────────────────┐
│                        PERUN PLATFORM                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐      │
│  │   Protocol   │    │  Transport   │    │     SDK      │      │
│  │              │    │              │    │              │      │
│  │ • Packets    │    │ • Unix       │    │ • Python     │      │
│  │ • Handshake  │    │ • TCP        │    │ • Rust       │      │
│  │ • Versioning │    │ • WebSocket  │    │ • C          │      │
│  └──────────────┘    └──────────────┘    └──────────────┘      │
│                                                                 │
│  ┌──────────────┐    ┌──────────────┐                          │
│  │    Server    │    │    Client    │                          │
│  │              │    │              │                          │
│  │ • Sessions   │    │ • Native     │                          │
│  │ • Relay      │    │ • WASM       │                          │
│  │ • Auth       │    │ • Rendering  │                          │
│  └──────────────┘    └──────────────┘                          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                    EMULATOR CORES (Separate Repos)              │
├─────────────────────────────────────────────────────────────────┤
│  chip8-core/     gameboy-core/     gba-core/     nes-core/     │
│  (Python)        (Rust)            (C++)         (Any lang)    │
└─────────────────────────────────────────────────────────────────┘
```

---

## Emulator Cores: Language-Agnostic Model

### Design Principle

Cores are **separate processes** that communicate via the Perun protocol. This enables:
- **Any programming language** (Python, Rust, C++, Go, etc.)
- **Independent development** (separate repos, own CI/CD)
- **Hot-swapping** (restart core without restarting server)

### Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        HOST MACHINE                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────┐      Unix Socket      ┌─────────────────────┐ │
│  │ Chip8 Core  │◄─────────────────────►│                     │ │
│  │ (Python)    │                       │                     │ │
│  └─────────────┘                       │    Perun Server     │ │
│                                        │                     │ │
│  ┌─────────────┐      Unix Socket      │    • Relays frames  │ │
│  │ GameBoy     │◄─────────────────────►│    • Routes input   │ │
│  │ (Rust)      │                       │    • Manages cores  │ │
│  └─────────────┘                       │                     │ │
│                                        └──────────┬──────────┘ │
│                                                   │             │
└───────────────────────────────────────────────────┼─────────────┘
                                                    │ TCP/WebSocket
                                          ┌─────────▼─────────┐
                                          │  Remote Clients   │
                                          │  (Browser/Native) │
                                          └───────────────────┘
```

### Git Submodules Structure

Cores live in separate repositories, included as submodules:

```
perun/
├── cores/                          # Git submodules
│   ├── chip8-core/                → github.com/you/chip8-core
│   │   ├── src/
│   │   ├── pyproject.toml
│   │   └── README.md
│   ├── gameboy-core/              → github.com/you/gameboy-core  
│   │   ├── src/
│   │   ├── Cargo.toml
│   │   └── README.md
│   └── .gitmodules
│
├── sdk/                            # SDKs for each language
│   ├── python/perun_sdk/
│   ├── rust/perun-sdk/
│   └── c/libperun/
```

### The Protocol IS The Interface

Since cores run as separate processes, the **Perun protocol** defines the interface:

| Interaction | Protocol Message |
|-------------|------------------|
| Send frame | `0x01` VideoFrame packet |
| Receive input | `0x03` InputEvent packet |
| Send audio | `0x02` AudioChunk packet |
| Get/set config | `0x04` Config packet |

### SDK Abstracts Protocol Details

Core developers use an SDK that hides socket/protocol complexity:

```python
# Python core - no socket code visible
from perun_sdk import PerunCore, PerunConnection

class Chip8(PerunCore):
    def tick(self, buttons: int) -> tuple[bytes, bytes]:
        self.step(buttons)
        return self.get_framebuffer(), self.get_audio()

if __name__ == "__main__":
    core = Chip8()
    core.load_rom("game.ch8")
    PerunConnection("unix:///tmp/perun.sock").run(core)
```

```rust
// Rust core - SDK handles everything
use perun_sdk::{PerunCore, PerunConnection};

struct GameBoy { /* ... */ }

impl PerunCore for GameBoy {
    fn tick(&mut self, buttons: u16) -> (Vec<u8>, Vec<i16>) {
        self.step(buttons);
        (self.framebuffer.clone(), self.audio_buffer.drain(..).collect())
    }
}

fn main() {
    let core = GameBoy::new("game.gb");
    PerunConnection::new("unix:///tmp/perun.sock").run(core);
}
```

### Running Cores

```bash
# Start server
./perun-server --listen unix:///tmp/perun.sock

# Start core (separate process)
python cores/chip8-core/main.py game.ch8

# Or for Rust core
./cores/gameboy-core/target/release/gameboy-core game.gb
```

### Core Requirements

A valid Perun core must:
1. Connect to server via Unix socket or TCP
2. Complete handshake with version + capabilities
3. Send video frames at consistent FPS
4. Respond to input events
5. Optionally: send audio, support debug info

## Data Flow

```
Emulator Core                    Perun Server                    Perun Client
     │                                │                               │
     │──── PERUN_HELLO ──────────────>│                               │
     │<─── OK + Version ──────────────│                               │
     │                                │                               │
     │                                │<──── PERUN_HELLO ─────────────│
     │                                │───── OK + Version ───────────>│
     │                                │                               │
     ├────────────────────────────────┼───────────────────────────────┤
     │              MAIN LOOP         │                               │
     ├────────────────────────────────┼───────────────────────────────┤
     │                                │                               │
     │──── 0x01 Video Frame ─────────>│──── 0x01 Video Frame ────────>│
     │                                │                               │
     │<─── 0x03 Input Event ──────────│<─── 0x03 Input Event ─────────│
     │                                │                               │
     │──── 0x02 Audio Chunk ─────────>│──── 0x02 Audio Chunk ────────>│
     │                                │                               │
```

---

## Protocol Specification

### Design Principles
- **TCP_NODELAY** on all connections (disable Nagle's algorithm)
- **LZ4 compression** for all frame data
- **Delta frames** when beneficial (XOR previous → compress)
- **Latest-wins input** (use highest sequence, ignore stale)
- **2-frame client buffer** for jitter protection

### Handshake
```
Core/Client → Server: [PERUN_HELLO][Version:2][Capabilities:2]
Server → Core/Client: [OK][Version:2][Capabilities:2] | [ERROR][Len:2][Message]
```

**Capabilities flags:**
- `0x01`: Supports delta frames
- `0x02`: Supports audio streaming
- `0x04`: Supports debug info

### Packet Format
```
┌─────────┬─────────┬──────────┬──────────┬─────────────────────┐
│ Type(1) │ Flags(1)│ Seq(2)   │ Length(4)│ Payload (variable)  │
└─────────┴─────────┴──────────┴──────────┴─────────────────────┘
```

**Flags byte:**
- Bit 0: Delta frame (1 = XOR delta, 0 = full frame)
- Bit 1-2: Compression level (0=none, 1=fast, 2=default, 3=high)
- Bit 3-7: Reserved

### Packet Types
| Type | Name | Direction | Payload |
|------|------|-----------|---------|
| 0x01 | Video Frame | Core→Server→Client | `[Width:2][Height:2][LZ4_Data...]` |
| 0x02 | Audio Chunk | Core→Server→Client | `[SampleRate:2][Channels:1][Samples...]` |
| 0x03 | Input Event | Client→Server→Core | `[Buttons:2][Reserved:2]` |
| 0x04 | Config | Bidirectional | `[Key:1][ValueLen:2][Value...]` |
| 0x05 | Debug Info | Core→Client | `[Registers, memory, etc.]` |

### Input Handling (Latest-Wins)
```
Client sends input every 16ms with incrementing sequence number.
Server/Core uses packet with HIGHEST sequence, ignores older.
Prevents input queue buildup during network hiccups.
```

### Frame Compression Strategy
```python
# Sender logic
delta = xor(current_frame, previous_frame)
delta_compressed = lz4.compress(delta)
full_compressed = lz4.compress(current_frame)

if len(delta_compressed) < len(full_compressed) * 0.7:
    send(type=0x01, flags=DELTA, data=delta_compressed)
else:
    send(type=0x01, flags=FULL, data=full_compressed)
```

---

## Transport Layer

### Transport Stack
```
┌─────────────────────────────────────────────┐
│             PERUN PROTOCOL v1               │
├─────────────────────────────────────────────┤
│  • Same binary format on all transports     │
│  • TCP_NODELAY enabled everywhere           │
│  • LZ4 + delta compression                  │
└─────────────────────────────────────────────┘
         │              │              │
    Unix Socket       TCP          WebSocket
    (local dev)    (network)       (browser)
```

### Transport Comparison
| Transport | Use Case | Port | Latency | Notes |
|-----------|----------|------|---------|-------|
| Unix Socket | Local dev | `/tmp/perun.sock` | ~0.1ms | Lowest latency |
| TCP | LAN/Internet | `9000` | 5-20ms | + TCP_NODELAY |
| WebSocket | Browser | `9001` | 10-30ms | Required for WASM |

### Future Optimization (Phase 2.5+)
| Transport | When | Benefit |
|-----------|------|--------|
| UDP | Native optimization | 5-10ms lower latency |
| WebRTC DataChannel | Browser optimization | 10-20ms lower, P2P |

### Connection Settings
```cpp
// All TCP connections must set:
setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

// Recommended server-side (Linux):
// sysctl net.ipv4.tcp_congestion_control=bbr
```

---

## Repository Structure (Target)

```
perun/
├── include/
│   └── Perun/
│       ├── Protocol/
│       │   ├── Packets.h           # Packet definitions
│       │   └── Handshake.h         # Handshake logic
│       ├── Transport/
│       │   ├── ITransport.h        # Abstract interface
│       │   ├── UnixTransport.h
│       │   ├── TcpTransport.h
│       │   └── WebSocketTransport.h
│       └── Client/
│           ├── Renderer.h          # Frame rendering only
│           └── AudioPlayer.h       # PCM streaming
│
├── src/
│   ├── Protocol/
│   ├── Transport/
│   ├── Server/                     # Main server application
│   └── Client/                     # Native client
│
├── cores/                          # Git submodules (emulator cores)
│   ├── chip8-core/                → separate repo
│   ├── gameboy-core/              → separate repo
│   └── .gitmodules
│
├── sdk/
│   ├── python/                     # pip install perun-sdk
│   │   └── perun_sdk/
│   │       ├── __init__.py
│   │       ├── connection.py
│   │       ├── protocol.py
│   │       └── core.py             # PerunCore base class
│   ├── rust/                       # perun-sdk crate
│   │   ├── Cargo.toml
│   │   └── src/lib.rs
│   └── c/                          # libperun.h / libperun.a
│       ├── include/perun_core.h
│       └── src/perun_core.c
│
├── tests/
│   ├── unit/
│   │   ├── protocol/
│   │   └── transport/
│   ├── integration/
│   │   ├── mock_core.py            # Fake emulator for testing
│   │   └── test_client.cpp
│   └── conformance/
│       └── protocol_test.py        # Core compatibility tests
│
├── web/                            # WASM client
│   ├── index.html
│   ├── perun.js
│   └── build/
│
├── docs/
│   ├── planning/
│   │   ├── architecture_overview.md
│   │   └── implementation_plan.md
│   ├── protocol.md
│   └── building-cores.md           # Guide for core developers
│
└── CMakeLists.txt
```

---

## Files to Modify/Remove (From Current Codebase)

| File/Dir | Action | Reason |
|----------|--------|--------|
| `src/ImGui/` | **Keep** | Debug overlay for development |
| `include/Perun/ImGui/` | **Keep** | Debug overlay |
| `include/Perun/Math/` | Remove | Protocol just uses ints |
| `src/Graphics/` (most) | Simplify | Keep texture upload, remove primitives |
| `examples/main.cpp` | Replace | Proper examples needed |

---

## Debug UI (ImGui Overlay)

Optional debug overlay for emulator development:

```
┌─────────────────────────────────────────┐
│  Perun Client (Debug Mode)              │
├─────────────────────────────────────────┤
│ ┌─────────┐  ┌────────────────────────┐ │
│ │ Game    │  │ Registers              │ │
│ │ Display │  │ PC: 0x0200  SP: 0x0F   │ │
│ │         │  │ V0-VF: ...             │ │
│ │         │  ├────────────────────────┤ │
│ │         │  │ Memory View            │ │
│ │         │  │ 0x000: 00 E0 A2 2A ... │ │
│ └─────────┘  └────────────────────────┘ │
│ [Pause] [Step] [Breakpoints] [FPS: 60] │
└─────────────────────────────────────────┘
```

**Features:**
- Register inspection (sent via 0x04 Config packet)
- Memory hexdump
- Breakpoint management
- Frame stepping
- Performance metrics

**Enabled via:** `--debug` flag or config

---

## SDK Interface (What Core Authors Use)

```python
# Python SDK example
from perun_sdk import PerunCore, PerunConnection

class MyEmulator(PerunCore):
    def __init__(self, rom_path: str):
        self.load_rom(rom_path)
    
    def tick(self, input_state: int) -> tuple[bytes, bytes]:
        """Run one frame. Return (vram, audio)."""
        self.step()
        return (self.get_vram(), self.get_audio())
    
    def get_config(self) -> dict:
        return {"width": 256, "height": 224, "fps": 60}

# Usage
core = MyEmulator("game.rom")
conn = PerunConnection("tcp://localhost:9000")
conn.run(core)  # Handles main loop, protocol, everything
```

---

## Success Metrics

| Phase | Success Criteria |
|-------|------------------|
| Phase 2 | Mock core → TCP → Test client works |
| Phase 3 | WASM client plays in browser |
| Phase 4 | Multiple users, persistent saves |
| Phase 5 | Chip8 + GameBoy cores published |
