# Perun - Universal Emulator Frontend Platform

**Perun** is a high-performance, network-transparent emulator frontend platform built in Rust. It enables emulator cores to run on a server while streaming video, audio, and input to clients (Web/WASM, Native) with minimal latency.

## Features

-   **High Performance**: Uses `ShmState` (Shared Memory) for zero-copy communication between emulator cores and the server locally.
-   **Web Native**: Fully functional WASM client runs in the browser.
-   **Rust Ecosystem**: Server, Client, and SDK are all pure Rust.
-   **Unified CLI**: Managing the entire system is done via `perun-cli`.

## Architecture

```
┌─────────────┐         ┌──────────────┐         ┌──────────────┐
│  Emulator   │ <─SHM─> │ Perun Server │ <─Net─> │ Perun Client │
│ (Adapter)   │         │   (Rust)     │         │ (WASM/Native)│
└─────────────┘         └──────────────┘         └──────────────┘
```

## Quick Start

### Prerequisites
-   Rust (stable)
-   `wasm-pack` (for building the web client)

### Usage

The `perun-cli` is your main entrypoint. It handles building components, starting the server, and launching the emulator core.

#### 1. Start NES Demo
```bash
cargo run --bin perun-cli -- start nes path/to/rom.nes
```

#### 2. Start Chip-8 Demo
```bash
cargo run --bin perun-cli -- start chip8 path/to/rom.ch8
```

#### 3. Start Custom Core
You can launch any compatible core adapter binary:
```bash
cargo run --bin perun-cli -- start my-core path/to/rom --width 320 --height 240
```
*(This looks for an executable named `my-core` in your path)*

### Building Components
To just build everything without running:
```bash
cargo run --bin perun-cli -- build
```

## Developing Cores

Perun uses an **Adapter Pattern**. To support a new emulator, create a small Rust binary dealing with `perun-core` that:
1.  Implements the `PerunCore` trait.
2.  Links to your emulator library.
3.  Handles the main loop via `perun_core::run`.

See `perun-core` documentation for details.

## License
MIT