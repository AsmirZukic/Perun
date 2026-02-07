"""Protocol layer implementation for Perun SDK

Implements packet structures, serialization, and handshake protocol
matching the C++ implementation.
"""

import struct
from enum import IntEnum, IntFlag
from typing import List, Tuple, Optional
from dataclasses import dataclass


class PacketType(IntEnum):
    """Packet types matching C++ Protocol::PacketType"""
    VideoFrame = 0x01
    AudioChunk = 0x02
    InputEvent = 0x03
    Config = 0x04
    DebugInfo = 0x05


class PacketFlags(IntFlag):
    """Packet flags matching C++ Protocol::PacketFlags"""
    Compressed = 0x01
    DeltaFrame = 0x02


class Capabilities(IntFlag):
    """Capability flags matching C++ Protocol::Capabilities"""
    CAP_DELTA = 0x01
    CAP_AUDIO = 0x02
    CAP_DEBUG = 0x04


PROTOCOL_VERSION = 1
MAGIC_HELLO = b"PERUN_HELLO"


@dataclass
class PacketHeader:
    """Packet header (8 bytes fixed size)"""
    type: PacketType
    flags: int
    sequence: int
    length: int
    
    def serialize(self) -> bytes:
        """Serialize header to bytes (big-endian)"""
        return struct.pack(
            '>BBHi',  # B=uint8, H=uint16, i=uint32 (big-endian)
            self.type,
            self.flags,
            self.sequence,
            self.length
        )
    
    @staticmethod
    def deserialize(data: bytes) -> 'PacketHeader':
        """Deserialize header from bytes"""
        type_val, flags, sequence, length = struct.unpack('>BBHi', data[:8])
        return PacketHeader(
            type=PacketType(type_val),
            flags=flags,
            sequence=sequence,
            length=length
        )


@dataclass
class VideoFramePacket:
    """Video frame packet with optional compression"""
    width: int
    height: int
    compressed_data: bytes
    
    def serialize(self) -> bytes:
        """Serialize to bytes (big-endian)"""
        return struct.pack('>HH', self.width, self.height) + self.compressed_data
    
    @staticmethod
    def deserialize(data: bytes) -> 'VideoFramePacket':
        """Deserialize from bytes"""
        width, height = struct.unpack('>HH', data[:4])
        return VideoFramePacket(
            width=width,
            height=height,
            compressed_data=data[4:]
        )


@dataclass
class InputEventPacket:
    """Input event packet with button state"""
    buttons: int
    reserved: int = 0
    
    def serialize(self) -> bytes:
        """Serialize to bytes (big-endian)"""
        return struct.pack('>HH', self.buttons, self.reserved)
    
    @staticmethod
    def deserialize(data: bytes) -> 'InputEventPacket':
        """Deserialize from bytes"""
        buttons, reserved = struct.unpack('>HH', data[:4])
        return InputEventPacket(buttons=buttons, reserved=reserved)


@dataclass
class AudioChunkPacket:
    """Audio chunk packet with samples"""
    sample_rate: int
    channels: int
    samples: List[int]  # List of int16 samples
    
    def serialize(self) -> bytes:
        """Serialize to bytes (big-endian)"""
        data = struct.pack('>HB', self.sample_rate, self.channels)
        for sample in self.samples:
            data += struct.pack('>h', sample)  # h = int16
        return data
    
    @staticmethod
    def deserialize(data: bytes) -> 'AudioChunkPacket':
        """Deserialize from bytes"""
        sample_rate, channels = struct.unpack('>HB', data[:3])
        num_samples = (len(data) - 3) // 2
        samples = []
        for i in range(num_samples):
            offset = 3 + i * 2
            sample = struct.unpack('>h', data[offset:offset+2])[0]
            samples.append(sample)
        return AudioChunkPacket(
            sample_rate=sample_rate,
            channels=channels,
            samples=samples
        )


class Handshake:
    """Handshake protocol implementation"""
    
    @staticmethod
    def create_hello(version: int = PROTOCOL_VERSION, capabilities: int = 0) -> bytes:
        """Create client hello message"""
        return MAGIC_HELLO + struct.pack('>HH', version, capabilities)
    
    @staticmethod
    def process_response(data: bytes) -> Tuple[bool, Optional[int], Optional[int], Optional[str]]:
        """
        Process server response to handshake
        
        Returns: (success, version, capabilities, error)
        """
        if len(data) < 2:
            return False, None, None, "Response too short"
        
        if data[:2] == b'OK':
            if len(data) < 6:
                return False, None, None, "OK response too short"
            version, capabilities = struct.unpack('>HH', data[2:6])
            return True, version, capabilities, None
        elif data[:5] == b'ERROR':
            error = data[5:].decode('utf-8', errors='replace').rstrip('\x00')
            return False, None, None, error
        else:
            return False, None, None, "Invalid response"


def compute_delta(current: bytes, previous: bytes) -> bytes:
    """Compute XOR delta between two frames"""
    if len(current) != len(previous):
        raise ValueError("Frame sizes must match for delta compression")
    
    return bytes(c ^ p for c, p in zip(current, previous))


def apply_delta(delta: bytes, previous: bytes) -> bytes:
    """Apply XOR delta to previous frame"""
    if len(delta) != len(previous):
        raise ValueError("Delta and previous frame sizes must match")
    
    return bytes(d ^ p for d, p in zip(delta, previous))
