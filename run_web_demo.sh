#!/bin/bash
killall -9 perun-server python3 2>/dev/null
rm -f /tmp/perun.sock

# 1. Start Server with WebSocket support
# --tcp :8080 for Chip-8 (native TCP)
# --ws :8081 for Web Client (WebSocket)
# Use full path to server if running from project root
./build/perun-server --unix /tmp/perun.sock --tcp :8080 --ws :8081 > server.log 2>&1 &
SRV_PID=$!
echo "Server started (PID $SRV_PID)..."
sleep 1

# 2. Start Chip-8 Emulator (New implementation)
ROM_DIR="roms"
ROM_FILE="${1:-1-chip8-logo.ch8}"

if [ ! -f "$ROM_DIR/$ROM_FILE" ]; then
    echo "Error: ROM '$ROM_DIR/$ROM_FILE' not found!"
    echo "Available ROMs in $ROM_DIR/:"
    ls -1 $ROM_DIR
    
    # Kill server if we fail here
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
echo "OPEN BROWSER TO: http://localhost:8000"
echo "You should see the Chip-8 output in the browser!"
echo "---------------------------------------------------"

# Keep running until user kills
wait $SRV_PID
