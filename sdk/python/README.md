# Perun SDK for Python

Python SDK for connecting emulator cores to the Perun Universal Emulator Frontend Platform.

## Features

- **Protocol Implementation**: Full implementation of Perun's binary protocol
  - PacketHeader, VideoFramePacket, InputEventPacket, AudioChunkPacket
  - Big-endian serialization matching C++ server
  - Handshake with capability negotiation

- **Transport Support**: 
  - Unix domain sockets (local IPC)
  - TCP/IP sockets (network)
  - Non-blocking I/O

- **Easy Integration**: Simple API for emulator cores
  ```python
  from perun_sdk import PerunConnection, VideoFramePacket
  
  conn = PerunConnection()
  conn.connect_unix("/tmp/perun.sock")
  
  packet = VideoFramePacket(width=640, height=480, compressed_data=frame_data)
  conn.send_video_frame(packet)
  ```

## Installation

```bash
cd sdk/python
pip install -e .
```

## Quick Start

See `examples/test_core.py` for a complete example. Basic usage:

```python
from perun_sdk import PerunConnection, VideoFramePacket

# Connect to server
conn = PerunConnection()
if not conn.connect_unix("/tmp/perun.sock"):
    conn.connect_tcp("127.0.0.1", 8080)

# Send frames
while running:
    packet = VideoFramePacket(
        width=640,
        height=480,
        compressed_data=frame_data
    )
    conn.send_video_frame(packet)
    
    # Receive input events
    result = conn.receive_packet()
    if result:
        packet_type, payload = result
        # Handle packet...

# Cleanup
conn.close()
```

## Requirements

- Python 3.7+
- No external dependencies (uses Python stdlib only)

## License

MIT
