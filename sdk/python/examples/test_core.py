#!/usr/bin/env python3
"""
Example Chip-8 style emulator core for Perun

This demonstrates how to use the Perun SDK to create an emulator core.
For simplicity, this just generates animated test patterns rather than
implementing a full emulator.
"""

import time
import random
from perun_sdk import PerunConnection, VideoFramePacket, InputEventPacket


def generate_test_pattern(frame_number: int, width: int = 640, height: int = 480) -> bytes:
    """Generate a colorful test pattern"""
    data = bytearray(width * height * 4)
    
    for y in range(height):
        for x in range(width):
            i = (y * width + x) * 4
            # Animated gradient pattern
            data[i + 0] = (x + frame_number) % 256      # R
            data[i + 1] = (y + frame_number) % 256      # G
            data[i + 2] = (x + y + frame_number) % 256  # B
            data[i + 3] = 255                            # A
    
    return bytes(data)


def main():
    print("[TestCore] Starting Perun SDK test core")
    
    # Create connection
    conn = PerunConnection()
    
    # Connect to server (try Unix socket first, then TCP)
    if not conn.connect_unix("/tmp/perun.sock"):
        print("[TestCore] Unix socket connection failed, trying TCP...")
        if not conn.connect_tcp("127.0.0.1", 8080):
            print("[TestCore] Failed to connect")
            return 1
    
    print(f"[TestCore] Connected! Supports delta: {conn.supports_delta()}")
    
    # Main loop - send frames at 60 FPS
    frame_number = 0
    previous_frame = None
    
    try:
        while True:
            start_time = time.time()
            
            # Generate frame data
            frame_data = generate_test_pattern(frame_number, 640, 480)
            
            # Create and send video frame packet
            packet = VideoFramePacket(
                width=640,
                height=480,
                compressed_data=frame_data  # In real usage, compress with LZ4
            )
            
            if conn.send_video_frame(packet):
                if frame_number % 60 == 0:  # Log every second
                    print(f"[TestCore] Sent frame {frame_number}")
            else:
                print("[TestCore] Failed to send frame, disconnecting...")
                break
            
            # Check for incoming packets (input events, etc.)
            while True:
                result = conn.receive_packet()
                if result is None:
                    break
                
                packet_type, payload = result
                if packet_type == 0x03:  # InputEvent
                    input_packet = InputEventPacket.deserialize(payload)
                    print(f"[TestCore] Received input: buttons=0x{input_packet.buttons:04x}")
            
            frame_number += 1
            
            # Target 60 FPS
            elapsed = time.time() - start_time
            sleep_time = max(0, (1.0 / 60.0) - elapsed)
            if sleep_time > 0:
                time.sleep(sleep_time)
            
    except KeyboardInterrupt:
        print("\n[TestCore] Interrupted by user")
    finally:
        conn.close()
        print("[TestCore] Disconnected")
    
    return 0


if __name__ == "__main__":
    exit(main())
