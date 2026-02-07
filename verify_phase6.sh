#!/bin/bash
# Clean up
killall -9 perun-server perun-client 2>/dev/null
rm -f /tmp/perun.sock

# 1. Start Server (Headless)
./build/perun-server --headless --unix /tmp/perun.sock --tcp :8080 > server.log 2>&1 &
SERVER_PID=$!
echo "Started Server (PID $SERVER_PID)"
sleep 2

# 2. Start Emulator Core (Python)
# Uses Unix socket by default
PYTHONPATH=sdk/python python3 sdk/python/examples/test_core.py > core.log 2>&1 &
CORE_PID=$!
echo "Started Emulator Core (PID $CORE_PID)"
sleep 2

# 3. Start Client (C++)
# Connects via TCP to localhost:8080
./build/perun-client --tcp 127.0.0.1:8080 > client.log 2>&1 &
CLIENT_PID=$!
echo "Started Client (PID $CLIENT_PID)"

# 4. Run for 5 seconds
sleep 5

# 5. Stop everything
kill $SERVER_PID $CORE_PID $CLIENT_PID
echo "Stopped processes"

# 6. Show results
echo "=== Server Log (Tail) ==="
tail -n 20 server.log
echo ""
echo "=== Core Log (Tail) ==="
tail -n 20 core.log
echo ""
echo "=== Client Log (Tail) ==="
tail -n 20 client.log
