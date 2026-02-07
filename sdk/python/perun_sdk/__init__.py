"""Perun SDK for Python

A Python SDK for connecting emulator cores to the Perun Universal Frontend Platform.
"""

__version__ = "0.1.0"

from .protocol import (
    PacketType,
    PacketFlags,
    Capabilities,
    PacketHeader,
    VideoFramePacket,
    InputEventPacket,
    AudioChunkPacket,
    Handshake,
    apply_delta,
    compute_delta,
)

from .connection import PerunConnection

__all__ = [
    "PacketType",
    "PacketFlags",
    "Capabilities",
    "PacketHeader",
    "VideoFramePacket",
    "InputEventPacket",
    "AudioChunkPacket",
    "Handshake",
    "PerunConnection",
    "apply_delta",
    "compute_delta",
]
