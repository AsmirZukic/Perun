//! Protocol types for Perun server
//! 
//! Implements the wire protocol matching the C++ implementation.

pub mod packets;
pub mod handshake;

pub use packets::*;
pub use handshake::*;

/// Protocol version
pub const PROTOCOL_VERSION: u16 = 1;

/// Capability flags
pub mod capabilities {
    pub const CAP_DELTA: u16 = 0x01;
    pub const CAP_AUDIO: u16 = 0x02;
    pub const CAP_DEBUG: u16 = 0x04;
}
