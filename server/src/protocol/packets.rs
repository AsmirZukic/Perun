//! Packet types and serialization
//!
//! Wire format:
//! - PacketHeader: 8 bytes (type:1, flags:1, sequence:2, length:4)
//! - Payload: variable length

use bytes::{Buf, BufMut};
use thiserror::Error;

/// Packet types matching C++ enum
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum PacketType {
    VideoFrame = 0x01,
    AudioChunk = 0x02,
    InputEvent = 0x03,
    Config = 0x04,
    DebugInfo = 0x05,
}

impl TryFrom<u8> for PacketType {
    type Error = ProtocolError;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0x01 => Ok(PacketType::VideoFrame),
            0x02 => Ok(PacketType::AudioChunk),
            0x03 => Ok(PacketType::InputEvent),
            0x04 => Ok(PacketType::Config),
            0x05 => Ok(PacketType::DebugInfo),
            _ => Err(ProtocolError::InvalidPacketType(value)),
        }
    }
}

/// Packet flags
pub mod flags {
    pub const FLAG_DELTA: u8 = 0x01;
    pub const FLAG_COMPRESS_1: u8 = 0x02;
    pub const FLAG_COMPRESS_2: u8 = 0x04;
}

/// Protocol errors
#[derive(Debug, Error)]
pub enum ProtocolError {
    #[error("Invalid packet type: {0}")]
    InvalidPacketType(u8),
    #[error("Buffer too small: need {needed}, have {have}")]
    BufferTooSmall { needed: usize, have: usize },
    #[error("Invalid data")]
    InvalidData,
}

/// Packet header (8 bytes)
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PacketHeader {
    pub packet_type: PacketType,
    pub flags: u8,
    pub sequence: u16,
    pub length: u32,
}

impl PacketHeader {
    pub const SIZE: usize = 8;

    /// Serialize header to bytes
    pub fn serialize(&self) -> [u8; 8] {
        let mut buf = [0u8; 8];
        buf[0] = self.packet_type as u8;
        buf[1] = self.flags;
        buf[2..4].copy_from_slice(&self.sequence.to_be_bytes());
        buf[4..8].copy_from_slice(&self.length.to_be_bytes());
        buf
    }

    /// Deserialize header from bytes
    pub fn deserialize(data: &[u8]) -> Result<Self, ProtocolError> {
        if data.len() < Self::SIZE {
            return Err(ProtocolError::BufferTooSmall {
                needed: Self::SIZE,
                have: data.len(),
            });
        }

        let packet_type = PacketType::try_from(data[0])?;
        let flags = data[1];
        let sequence = u16::from_be_bytes([data[2], data[3]]);
        let length = u32::from_be_bytes([data[4], data[5], data[6], data[7]]);

        Ok(Self {
            packet_type,
            flags,
            sequence,
            length,
        })
    }
}

/// Video frame packet
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct VideoFramePacket {
    pub width: u16,
    pub height: u16,
    pub is_delta: bool,
    pub data: Vec<u8>,
}

impl VideoFramePacket {
    /// Serialize to payload bytes (excluding header)
    pub fn serialize(&self) -> Vec<u8> {
        let mut buf = Vec::with_capacity(4 + self.data.len());
        buf.put_u16(self.width);
        buf.put_u16(self.height);
        buf.extend_from_slice(&self.data);
        buf
    }

    /// Deserialize from payload bytes
    pub fn deserialize(data: &[u8], flags: u8) -> Result<Self, ProtocolError> {
        if data.len() < 4 {
            return Err(ProtocolError::BufferTooSmall {
                needed: 4,
                have: data.len(),
            });
        }

        let mut cursor = std::io::Cursor::new(data);
        let width = cursor.get_u16();
        let height = cursor.get_u16();
        let frame_data = data[4..].to_vec();

        Ok(Self {
            width,
            height,
            is_delta: (flags & flags::FLAG_DELTA) != 0,
            data: frame_data,
        })
    }
}

/// Audio chunk packet
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AudioChunkPacket {
    pub sample_rate: u16,
    pub channels: u8,
    pub samples: Vec<i16>,
}

impl AudioChunkPacket {
    /// Serialize to payload bytes
    pub fn serialize(&self) -> Vec<u8> {
        let mut buf = Vec::with_capacity(3 + self.samples.len() * 2);
        buf.put_u16(self.sample_rate);
        buf.push(self.channels);
        for sample in &self.samples {
            buf.put_i16(*sample);
        }
        buf
    }

    /// Deserialize from payload bytes
    pub fn deserialize(data: &[u8]) -> Result<Self, ProtocolError> {
        if data.len() < 3 {
            return Err(ProtocolError::BufferTooSmall {
                needed: 3,
                have: data.len(),
            });
        }

        let sample_rate = u16::from_be_bytes([data[0], data[1]]);
        let channels = data[2];
        
        let sample_bytes = &data[3..];
        if sample_bytes.len() % 2 != 0 {
            return Err(ProtocolError::InvalidData);
        }

        let samples: Vec<i16> = sample_bytes
            .chunks_exact(2)
            .map(|chunk| i16::from_be_bytes([chunk[0], chunk[1]]))
            .collect();

        Ok(Self {
            sample_rate,
            channels,
            samples,
        })
    }
}

/// Input event packet
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct InputEventPacket {
    pub buttons: u16,
    pub reserved: u16,
}

impl InputEventPacket {
    /// Serialize to payload bytes
    pub fn serialize(&self) -> Vec<u8> {
        let mut buf = Vec::with_capacity(4);
        buf.put_u16(self.buttons);
        buf.put_u16(self.reserved);
        buf
    }

    /// Deserialize from payload bytes
    pub fn deserialize(data: &[u8]) -> Result<Self, ProtocolError> {
        if data.len() < 4 {
            return Err(ProtocolError::BufferTooSmall {
                needed: 4,
                have: data.len(),
            });
        }

        let buttons = u16::from_be_bytes([data[0], data[1]]);
        let reserved = u16::from_be_bytes([data[2], data[3]]);

        Ok(Self { buttons, reserved })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // ==================== PacketHeader Tests ====================

    #[test]
    fn test_packet_header_serialize_video_frame() {
        let header = PacketHeader {
            packet_type: PacketType::VideoFrame,
            flags: 0,
            sequence: 42,
            length: 1024,
        };

        let bytes = header.serialize();

        assert_eq!(bytes[0], 0x01); // VideoFrame
        assert_eq!(bytes[1], 0x00); // flags
        assert_eq!(u16::from_be_bytes([bytes[2], bytes[3]]), 42); // sequence
        assert_eq!(u32::from_be_bytes([bytes[4], bytes[5], bytes[6], bytes[7]]), 1024); // length
    }

    #[test]
    fn test_packet_header_roundtrip() {
        let original = PacketHeader {
            packet_type: PacketType::AudioChunk,
            flags: flags::FLAG_DELTA,
            sequence: 0xABCD,
            length: 0x12345678,
        };

        let bytes = original.serialize();
        let decoded = PacketHeader::deserialize(&bytes).unwrap();

        assert_eq!(original, decoded);
    }

    #[test]
    fn test_packet_header_deserialize_too_small() {
        let bytes = [0x01, 0x00, 0x00]; // Only 3 bytes

        let result = PacketHeader::deserialize(&bytes);

        assert!(matches!(result, Err(ProtocolError::BufferTooSmall { .. })));
    }

    #[test]
    fn test_packet_type_invalid() {
        let bytes = [0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00];

        let result = PacketHeader::deserialize(&bytes);

        assert!(matches!(result, Err(ProtocolError::InvalidPacketType(0xFF))));
    }

    // ==================== VideoFramePacket Tests ====================

    #[test]
    fn test_video_frame_roundtrip() {
        let original = VideoFramePacket {
            width: 64,
            height: 32,
            is_delta: false,
            data: vec![0xFF, 0x00, 0xAB, 0xCD],
        };

        let bytes = original.serialize();
        let decoded = VideoFramePacket::deserialize(&bytes, 0).unwrap();

        assert_eq!(original.width, decoded.width);
        assert_eq!(original.height, decoded.height);
        assert_eq!(original.data, decoded.data);
    }

    #[test]
    fn test_video_frame_delta() {
        let original = VideoFramePacket {
            width: 64,
            height: 32,
            is_delta: true,
            data: vec![0x12, 0x34],
        };

        let bytes = original.serialize();
        // Pass FLAG_DELTA (0x01) during deserialization
        let decoded = VideoFramePacket::deserialize(&bytes, flags::FLAG_DELTA).unwrap();

        assert_eq!(decoded.is_delta, true);
        assert_eq!(decoded.data, original.data);
    }

    #[test]
    fn test_video_frame_empty_data() {
        let original = VideoFramePacket {
            width: 320,
            height: 240,
            is_delta: false,
            data: vec![],
        };

        let bytes = original.serialize();
        assert_eq!(bytes.len(), 4); // Just width + height

        let decoded = VideoFramePacket::deserialize(&bytes, 0).unwrap();
        assert_eq!(decoded.width, 320);
        assert_eq!(decoded.height, 240);
        assert!(decoded.data.is_empty());
    }

    // ==================== AudioChunkPacket Tests ====================

    #[test]
    fn test_audio_chunk_roundtrip() {
        let original = AudioChunkPacket {
            sample_rate: 44100,
            channels: 2,
            samples: vec![100, -100, 32767, -32768],
        };

        let bytes = original.serialize();
        let decoded = AudioChunkPacket::deserialize(&bytes).unwrap();

        assert_eq!(original, decoded);
    }

    #[test]
    fn test_audio_chunk_mono() {
        let original = AudioChunkPacket {
            sample_rate: 22050,
            channels: 1,
            samples: vec![0, 1000, -1000],
        };

        let bytes = original.serialize();
        let decoded = AudioChunkPacket::deserialize(&bytes).unwrap();

        assert_eq!(original, decoded);
    }

    // ==================== InputEventPacket Tests ====================

    #[test]
    fn test_input_event_roundtrip() {
        let original = InputEventPacket {
            buttons: 0b1010_0101,
            reserved: 0,
        };

        let bytes = original.serialize();
        let decoded = InputEventPacket::deserialize(&bytes).unwrap();

        assert_eq!(original, decoded);
    }

    #[test]
    fn test_input_event_all_buttons() {
        let original = InputEventPacket {
            buttons: 0xFFFF,
            reserved: 0x1234,
        };

        let bytes = original.serialize();
        let decoded = InputEventPacket::deserialize(&bytes).unwrap();

        assert_eq!(original, decoded);
    }
}
