"""Connection management for Perun SDK

Handles transport layer connections and protocol-level communication.
"""

import socket
import struct
from typing import Optional, Callable
from .protocol import (
    PacketType,
    PacketHeader,
    VideoFramePacket,
    InputEventPacket,
    AudioChunkPacket,
    Handshake,
    PROTOCOL_VERSION,
    Capabilities,
)


class PerunConnection:
    """
    Connection to a Perun server
    
    Handles handshake, packet sending/receiving, and connection management.
    """
    
    def __init__(self):
        self._socket: Optional[socket.socket] = None
        self._connected = False
        self._handshake_complete = False
        self._capabilities = 0
        self._sequence = 0
        self._receive_buffer = bytearray()
        
    def connect_unix(self, socket_path: str, capabilities: int = Capabilities.CAP_DELTA) -> bool:
        """Connect to server via Unix socket"""
        try:
            self._socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self._socket.connect(socket_path)
            self._socket.setblocking(False)  # Non-blocking mode
            self._connected = True
            
            # Perform handshake
            return self._do_handshake(capabilities)
        except Exception as e:
            print(f"Connection failed: {e}")
            return False
    
    def connect_tcp(self, host: str, port: int, capabilities: int = Capabilities.CAP_DELTA) -> bool:
        """Connect to server via TCP"""
        try:
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._socket.connect((host, port))
            self._socket.setblocking(False)  # Non-blocking mode
            
            # Enable TCP_NODELAY for low latency
            self._socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self._connected = True
            
            # Perform handshake
            return self._do_handshake(capabilities)
        except Exception as e:
            print(f"Connection failed: {e}")
            return False
    
    def _do_handshake(self, capabilities: int) -> bool:
        """Perform handshake with server"""
        if not self._socket:
            return False
        
        # Send hello
        hello = Handshake.create_hello(PROTOCOL_VERSION, capabilities)
        self._socket.setblocking(True)  # Blocking for handshake
        self._socket.sendall(hello)
        
        # Receive response (up to 256 bytes)
        response = self._socket.recv(256)
        self._socket.setblocking(False)  # Back to non-blocking
        
        success, version, caps, error = Handshake.process_response(response)
        
        if success:
            self._handshake_complete = True
            self._capabilities = caps
            print(f"Handshake successful, caps: 0x{caps:04x}")
            return True
        else:
            print(f"Handshake failed: {error}")
            self.close()
            return False
    
    def close(self):
        """Close the connection"""
        if self._socket:
            self._socket.close()
            self._socket = None
        self._connected = False
        self._handshake_complete = False
    
    def is_connected(self) -> bool:
        """Check if connection is active"""
        return self._connected and self._handshake_complete
    
    def send_video_frame(self, packet: VideoFramePacket) -> bool:
        """Send video frame packet"""
        if not self.is_connected():
            return False
        
        payload = packet.serialize()
        return self._send_packet(PacketType.VideoFrame, payload)
    
    def send_input_event(self, packet: InputEventPacket) -> bool:
        """Send input event packet"""
        if not self.is_connected():
            return False
        
        payload = packet.serialize()
        return self._send_packet(PacketType.InputEvent, payload)
    
    def send_audio_chunk(self, packet: AudioChunkPacket) -> bool:
        """Send audio chunk packet"""
        if not self.is_connected():
            return False
        
        payload = packet.serialize()
        return self._send_packet(PacketType.AudioChunk, payload)
    
    def _send_packet(self, packet_type: PacketType, payload: bytes) -> bool:
        """Send a packet with header"""
        if not self._socket:
            return False
        
        try:
            header = PacketHeader(
                type=packet_type,
                flags=0,
                sequence=self._sequence,
                length=len(payload)
            )
            self._sequence += 1
            
            # Send header + payload matching non-blocking socket behavior
            # We want to ensure the whole packet is sent, so we handle partial sends
            data = header.serialize() + payload
            total_sent = 0
            
            import select
            
            while total_sent < len(data):
                try:
                    sent = self._socket.send(data[total_sent:])
                    if sent == 0:
                        raise ConnectionError("Socket closed during send")
                    total_sent += sent
                except BlockingIOError:
                    # Wait for socket to be writable
                    select.select([], [self._socket], [], 1.0)  # 1s timeout
            
            return True
        except Exception as e:
            print(f"Send failed: {e}")
            self.close()
            return False
    
    def receive_packet(self) -> Optional[tuple]:
        """
        Receive a packet if available (non-blocking)
        
        Returns: (packet_type, payload) or None if no complete packet available
        """
        result = self.receive_packet_header()
        if result:
            header, payload = result
            return (header.type, payload)
        return None

    def receive_packet_header(self) -> Optional[tuple]:
        """
        Receive a packet including header (non-blocking)
        
        Returns: (PacketHeader, payload) or None
        """
        if not self._socket:
            return None
        
        try:
            # Drain socket
            while True:
                try:
                    data = self._socket.recv(65536)
                    if not data:
                        # Connection closed
                        self.close()
                        return None
                    self._receive_buffer.extend(data)
                except BlockingIOError:
                    break
        except Exception as e:
            print(f"Receive error: {e}")
            self.close()
            return None
        
        # Check if we have a complete packet
        if len(self._receive_buffer) < 8:
            return None
        
        header = PacketHeader.deserialize(bytes(self._receive_buffer[:8]))
        
        if len(self._receive_buffer) < 8 + header.length:
            return None  # Wait for complete packet
        
        # Extract payload
        payload = bytes(self._receive_buffer[8:8+header.length])
        
        # Remove packet from buffer
        del self._receive_buffer[:8+header.length]
        
        return (header, payload)
    
    def get_capabilities(self) -> int:
        """Get negotiated capabilities"""
        return self._capabilities
    
    def supports_delta(self) -> bool:
        """Check if delta frames are supported"""
        return bool(self._capabilities & Capabilities.CAP_DELTA)
    
    def supports_audio(self) -> bool:
        """Check if audio is supported"""
        return bool(self._capabilities & Capabilities.CAP_AUDIO)
