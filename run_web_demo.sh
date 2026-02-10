#!/bin/bash
# Run Perun web demo using the RUST server
# This is a drop-in replacement for run_web_demo.sh to test the Rust implementation

set -e

killall -9 perun-server python3 2>/dev/null || true
rm -f /tmp/perun.sock
sleep 1

# Check if Rust server is built
# Check if Rust server is built
# Workspace build puts binaries in target/release at root
RUST_SERVER="./target/release/perun-server"
if [ ! -f "$RUST_SERVER" ]; then
    echo "Building Rust server..."
    # Build from root workspace
    cargo build --release
fi

# 1. Start Rust Server with TCP and WebSocket support
# --tcp :8080 for Chip-8 (native TCP)
# --ws :8081 for Web Client (WebSocket)
echo "Starting Rust server..."
$RUST_SERVER --tcp :8080 --ws :8081 --debug > server.log 2>&1 &
SRV_PID=$!
echo "Rust Server started (PID $SRV_PID)..."
sleep 2

# Check if server is running
if ! kill -0 $SRV_PID 2>/dev/null; then
    echo "ERROR: Rust server failed to start! Log:"
    cat server.log
    exit 1
fi

# 2. Start Chip-8 Emulator
ROM_DIR="roms"
ROM_FILE="${1:-8-scrolling.ch8}"

if [ ! -f "$ROM_DIR/$ROM_FILE" ]; then
    echo "Error: ROM '$ROM_DIR/$ROM_FILE' not found!"
    echo "Available ROMs in $ROM_DIR/:"
    ls -1 $ROM_DIR
    
    kill $SRV_PID
    exit 1
fi

PYTHONPATH=sdk/python:examples/python/Chip8_Python/src python3 -u -m chip8.cli --perun --tcp 127.0.0.1:8080 --no-delta "$ROM_DIR/$ROM_FILE" > chip8.log 2>&1 &
CHIP8_PID=$!
echo "Chip-8 Emulator started (PID $CHIP8_PID) running '$ROM_FILE'..."

# 3. Start Web Server
cd web
python3 -m http.server 8000 > ../web_server.log 2>&1 &
WEB_PID=$!
cd ..
echo "Web Server started (PID $WEB_PID) on port 8000..."

echo "---------------------------------------------------"
echo "🦀 RUST SERVER DEMO"
echo "---------------------------------------------------"
echo "OPEN BROWSER TO: http://localhost:8000"
echo "You should see the Chip-8 output in the browser!"
echo ""
echo "Server log: tail -f server.log"
echo "Chip8 log:  tail -f chip8.log"
echo "---------------------------------------------------"

# Cleanup function
cleanup() {
    echo ""
    echo "Shutting down..."
    kill $CHIP8_PID $WEB_PID $SRV_PID 2>/dev/null || true
}
trap cleanup EXIT

# Keep running until user kills
wait $SRV_PID
