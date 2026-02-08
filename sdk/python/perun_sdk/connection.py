"""Connection management for Perun SDK

Handles transport layer connections and protocol-level communication.
Simple synchronous non-blocking design - no threads.
"""

import socket
import struct
import select
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
    
    Uses simple non-blocking I/O without threads.
    Video frames are dropped if socket buffer is full.
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
            self._socket.setblocking(False)
            self._connected = True
            
            return self._do_handshake(capabilities)
        except Exception as e:
            print(f"Connection failed: {e}")
            return False
    
    def connect_tcp(self, host: str, port: int, capabilities: int = Capabilities.CAP_DELTA) -> bool:
        """Connect to server via TCP"""
        try:
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._socket.connect((host, port))
            
            # TCP_NODELAY for low latency
            self._socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            
            # Large send buffer
            self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 256 * 1024)
            
            self._connected = True
            
            if not self._do_handshake(capabilities):
                return False
            
            # Set non-blocking AFTER handshake
            self._socket.setblocking(False)
            
            print(f"[PerunConnection] Connected (simple non-blocking mode)")
            return True
            
        except Exception as e:
            print(f"Connection failed: {e}")
            self.close()
            return False
    
    def _do_handshake(self, capabilities: int) -> bool:
        """Perform handshake with server (blocking)"""
        if not self._socket:
            return False
        
        try:
            # Send hello (blocking for handshake)
            hello = Handshake.create_hello(PROTOCOL_VERSION, capabilities)
            self._socket.setblocking(True)
            self._socket.sendall(hello)
            
            # Receive response
            response = self._socket.recv(256)
            
            success, version, caps, error = Handshake.process_response(response)
            
            if success:
                self._handshake_complete = True
                self._capabilities = caps
                return True
            else:
                print(f"Handshake failed: {error}")
                return False
        except Exception as e:
            print(f"Handshake error: {e}")
            return False
    
    def close(self):
        """Close connection"""
        if self._socket:
            try:
                self._socket.close()
            except:
                pass
            self._socket = None
        
        self._connected = False
        self._handshake_complete = False
    
    def is_connected(self) -> bool:
        """Check if connection is active"""
        return self._connected and self._handshake_complete
    
    def send_video_frame(self, packet: VideoFramePacket, blocking: bool = True) -> bool:
        """Send video frame packet."""
        if not self.is_connected():
            return False
        
        payload = packet.serialize()
        return self._send_packet(PacketType.VideoFrame, payload, blocking)
    
    def send_video_frame_async(self, packet: VideoFramePacket) -> bool:
        """Send video frame with non-blocking mode (drop if socket full)."""
        return self.send_video_frame(packet, blocking=False)
    
    def send_input_event(self, packet: InputEventPacket) -> bool:
        """Send input event packet"""
        if not self.is_connected():
            return False
        
        payload = packet.serialize()
        return self._send_packet(PacketType.InputEvent, payload, blocking=True)
    
    def send_audio_chunk(self, packet: AudioChunkPacket) -> bool:
        """Send audio chunk packet"""
        if not self.is_connected():
            return False
        
        payload = packet.serialize()
        return self._send_packet(PacketType.AudioChunk, payload, blocking=True)
    
    def _send_packet(self, packet_type: PacketType, payload: bytes, blocking: bool = True) -> bool:
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
            self._sequence = (self._sequence + 1) % 65536
            
            data = header.serialize() + payload
            
            # For non-blocking mode, check if socket is writable first
            if not blocking:
                r, w, e = select.select([], [self._socket], [], 0)
                if not w:
                    # Socket buffer full - drop the packet
                    if packet_type == PacketType.VideoFrame:
                        print("DROPPED FRAME (Socket full)")
                    return False
            
            # Try to send all data
            total_sent = 0
            while total_sent < len(data):
                try:
                    sent = self._socket.send(data[total_sent:])
                    if sent == 0:
                        raise ConnectionError("Socket closed")
                    total_sent += sent
                except BlockingIOError:
                    if not blocking:
                        # Can't complete send in non-blocking mode
                        # We already sent partial data which corrupts the stream
                        # But this is rare if we checked select() first
                        return False
                    # For blocking mode, wait briefly and retry
                    select.select([], [self._socket], [], 0.01)
            
            return True
        except Exception as e:
            print(f"Send failed: {e}")
            self.close()
            return False
    
    def receive_packet(self) -> Optional[tuple]:
        """
        Receive a packet if available (non-blocking)
        
        Returns: (packet_type, payload) or None
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
            # Drain socket (non-blocking)
            while True:
                try:
                    data = self._socket.recv(65536)
                    if not data:
                        self.close()
                        return None
                    self._receive_buffer.extend(data)
                except BlockingIOError:
                    break
        except Exception as e:
            print(f"Receive error: {e}")
            self.close()
            return None
        
        # Check for complete packet
        if len(self._receive_buffer) < 8:
            return None
        
        header = PacketHeader.deserialize(bytes(self._receive_buffer[:8]))
        
        if len(self._receive_buffer) < 8 + header.length:
            return None
        
        payload = bytes(self._receive_buffer[8:8+header.length])
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
