import time
import math
import struct
import socket
import select
from typing import Optional, Tuple, List, Callable
from perun_sdk import PerunConnection, VideoFramePacket, PacketType, PacketHeader

class MockEmulatorCore:
    """
    A configurable Mock Emulator Core for testing the Perun Server.
    Can simulate specific behaviors, errors, and traffic patterns.
    """
    def __init__(self, address="/tmp/perun.sock", use_tcp=False, tcp_port=8080, tcp_host="127.0.0.1"):
        self.address = address
        self.use_tcp = use_tcp
        self.tcp_port = tcp_port
        self.tcp_host = tcp_host
        self.connection = PerunConnection()
        self.running = False
        self.width = 640
        self.height = 480
        self.frame_data = bytearray(self.width * self.height * 4)
        self.frame_count = 0
        self.connected = False
        self.last_input: Optional[object] = None
        self.on_packet_received: Optional[Callable[[PacketType, bytes], None]] = None

    def connect(self) -> bool:
        """Connect to the server"""
        if self.use_tcp:
            self.connected = self.connection.connect_tcp(self.tcp_host, self.tcp_port)
        else:
            self.connected = self.connection.connect_unix(self.address)
        return self.connected

    def disconnect(self):
        """Disconnect from the server"""
        if self.connected:
            self.connection.close()
            self.connected = False

    def generate_frame(self, pattern="gradient"):
        """Generate a test frame with specified pattern"""
        if pattern == "gradient":
            # Simple moving gradient
            offset = self.frame_count % 256
            for y in range(self.height):
                for x in range(self.width):
                    idx = (y * self.width + x) * 4
                    self.frame_data[idx] = (x + offset) % 256
                    self.frame_data[idx+1] = (y + offset) % 256
                    self.frame_data[idx+2] = (x + y) % 256
                    self.frame_data[idx+3] = 255
        elif pattern == "solid_red":
            self.frame_data[:] = b'\xFF\x00\x00\xFF' * (self.width * self.height)
        
        self.frame_count += 1

    def send_frame(self) -> bool:
        """Send the current frame to the server"""
        if not self.connected:
            return False
        
        packet = VideoFramePacket(
            width=self.width,
            height=self.height,
            compressed_data=self.frame_data
        )
        return self.connection.send_video_frame(packet)

    def process_incoming(self, timeout=0):
        """Process incoming packets"""
        result = self.connection.receive_packet(timeout=timeout)
        if result:
            packet_type, payload = result
            if self.on_packet_received:
                self.on_packet_received(packet_type, payload)
            return packet_type, payload
        return None

    def run_duration(self, seconds: float, fps=60):
        """Run the main loop for a specific duration"""
        start_time = time.time()
        frame_time = 1.0 / fps
        
        while time.time() - start_time < seconds:
            loop_start = time.time()
            
            if not self.connected:
                break
                
            self.generate_frame()
            if not self.send_frame():
                print("Failed to send frame")
                break
            
            self.process_incoming()
            
            elapsed = time.time() - loop_start
            sleep_time = max(0, frame_time - elapsed)
            time.sleep(sleep_time)

if __name__ == "__main__":
    # Self-test
    core = MockEmulatorCore()
    if core.connect():
        print("Mock core connected")
        core.run_duration(1.0)
        core.disconnect()
        print("Mock core finished")
    else:
        print("Failed to connect (ensure server is running)")
