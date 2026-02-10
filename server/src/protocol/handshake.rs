//! Handshake protocol
//!
//! Wire format (matching C++ implementation):
//! - HELLO: "PERUN_HELLO" (11 bytes) + version (2, big-endian) + capabilities (2, big-endian)
//! - OK:    "OK" (2 bytes) + version (2, big-endian) + capabilities (2, big-endian)
//! - ERROR: "ERROR" (5 bytes) + error_msg (null-terminated)

use super::ProtocolError;

const MAGIC_HELLO: &[u8; 11] = b"PERUN_HELLO";

/// Handshake result
#[derive(Debug)]
pub struct HandshakeResult {
    pub accepted: bool,
    pub version: u16,
    pub capabilities: u16,
    pub error: Option<String>,
}

/// Handshake utilities
pub struct Handshake;

impl Handshake {
    /// Create a HELLO message (client → server)
    pub fn create_hello(version: u16, capabilities: u16) -> Vec<u8> {
        let mut buf = Vec::with_capacity(15);
        buf.extend_from_slice(MAGIC_HELLO);
        buf.extend_from_slice(&version.to_be_bytes());  // Big-endian!
        buf.extend_from_slice(&capabilities.to_be_bytes());
        buf
    }

    /// Process a HELLO message (server-side)
    /// Returns negotiated result
    pub fn process_hello(
        data: &[u8],
        server_capabilities: u16,
    ) -> Result<HandshakeResult, ProtocolError> {
        if data.len() < 15 {
            return Err(ProtocolError::BufferTooSmall {
                needed: 15,
                have: data.len(),
            });
        }

        // Check magic
        if &data[0..11] != MAGIC_HELLO {
            return Ok(HandshakeResult {
                accepted: false,
                version: 0,
                capabilities: 0,
                error: Some("Invalid magic string".to_string()),
            });
        }

        // Big-endian!
        let version = u16::from_be_bytes([data[11], data[12]]);
        let client_caps = u16::from_be_bytes([data[13], data[14]]);

        // Negotiate capabilities (intersection)
        let negotiated_caps = client_caps & server_capabilities;

        Ok(HandshakeResult {
            accepted: true,
            version,
            capabilities: negotiated_caps,
            error: None,
        })
    }

    /// Create OK response (server → client)
    /// Format: "OK" + version (big-endian) + capabilities (big-endian)
    pub fn create_ok(version: u16, capabilities: u16) -> Vec<u8> {
        let mut buf = Vec::with_capacity(6);
        buf.extend_from_slice(b"OK");
        buf.extend_from_slice(&version.to_be_bytes());
        buf.extend_from_slice(&capabilities.to_be_bytes());
        buf
    }

    /// Create ERROR response (server → client)
    /// Format: "ERROR" + message (null-terminated)
    pub fn create_error(message: &str) -> Vec<u8> {
        let mut buf = Vec::with_capacity(6 + message.len());
        buf.extend_from_slice(b"ERROR");
        buf.extend_from_slice(message.as_bytes());
        buf.push(0); // Null terminator
        buf
    }

    /// Process response (client-side)
    pub fn process_response(data: &[u8]) -> Result<HandshakeResult, ProtocolError> {
        if data.len() < 2 {
            return Err(ProtocolError::BufferTooSmall {
                needed: 2,
                have: data.len(),
            });
        }

        // Check for OK response
        if data.len() >= 6 && &data[0..2] == b"OK" {
            let version = u16::from_be_bytes([data[2], data[3]]);
            let capabilities = u16::from_be_bytes([data[4], data[5]]);

            return Ok(HandshakeResult {
                accepted: true,
                version,
                capabilities,
                error: None,
            });
        }

        // Check for ERROR response
        if data.len() >= 5 && &data[0..5] == b"ERROR" {
            let error_msg = if data.len() > 5 {
                let msg_bytes = &data[5..];
                // Remove null terminator if present
                let end = msg_bytes.iter().position(|&b| b == 0).unwrap_or(msg_bytes.len());
                String::from_utf8_lossy(&msg_bytes[..end]).to_string()
            } else {
                "Unknown error".to_string()
            };

            return Ok(HandshakeResult {
                accepted: false,
                version: 0,
                capabilities: 0,
                error: Some(error_msg),
            });
        }

        Err(ProtocolError::InvalidData)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::protocol::capabilities::*;

    #[test]
    fn test_hello_format() {
        let hello = Handshake::create_hello(1, CAP_DELTA | CAP_AUDIO);

        assert_eq!(&hello[0..11], b"PERUN_HELLO");
        assert_eq!(u16::from_be_bytes([hello[11], hello[12]]), 1);
        assert_eq!(u16::from_be_bytes([hello[13], hello[14]]), CAP_DELTA | CAP_AUDIO);
        assert_eq!(hello.len(), 15);
    }

    #[test]
    fn test_process_hello_negotiates_capabilities() {
        let hello = Handshake::create_hello(1, CAP_DELTA | CAP_AUDIO | CAP_DEBUG);
        let server_caps = CAP_DELTA | CAP_DEBUG; // Server doesn't support audio

        let result = Handshake::process_hello(&hello, server_caps).unwrap();

        assert!(result.accepted);
        assert_eq!(result.version, 1);
        assert_eq!(result.capabilities, CAP_DELTA | CAP_DEBUG); // Intersection
    }

    #[test]
    fn test_process_hello_invalid_magic() {
        let bad_hello = b"WRONG_MAGIC1234";

        let result = Handshake::process_hello(bad_hello, CAP_DELTA).unwrap();

        assert!(!result.accepted);
        assert!(result.error.is_some());
    }

    #[test]
    fn test_ok_response_format() {
        let ok = Handshake::create_ok(1, CAP_DELTA | CAP_AUDIO);

        assert_eq!(&ok[0..2], b"OK");
        assert_eq!(u16::from_be_bytes([ok[2], ok[3]]), 1);
        assert_eq!(u16::from_be_bytes([ok[4], ok[5]]), CAP_DELTA | CAP_AUDIO);
        assert_eq!(ok.len(), 6);
    }

    #[test]
    fn test_ok_response_roundtrip() {
        let ok = Handshake::create_ok(1, CAP_DELTA);
        let result = Handshake::process_response(&ok).unwrap();

        assert!(result.accepted);
        assert_eq!(result.version, 1);
        assert_eq!(result.capabilities, CAP_DELTA);
    }

    #[test]
    fn test_error_response() {
        let error = Handshake::create_error("Version mismatch");
        let result = Handshake::process_response(&error).unwrap();

        assert!(!result.accepted);
        assert_eq!(result.error, Some("Version mismatch".to_string()));
    }

    #[test]
    fn test_full_handshake_flow() {
        // Client sends HELLO
        let hello = Handshake::create_hello(1, CAP_DELTA | CAP_AUDIO);

        // Server processes and responds
        let server_caps = CAP_DELTA | CAP_AUDIO | CAP_DEBUG;
        let server_result = Handshake::process_hello(&hello, server_caps).unwrap();
        assert!(server_result.accepted);

        // Server sends OK
        let ok = Handshake::create_ok(server_result.version, server_result.capabilities);

        // Client processes OK
        let client_result = Handshake::process_response(&ok).unwrap();
        assert!(client_result.accepted);
        assert_eq!(client_result.capabilities, CAP_DELTA | CAP_AUDIO);
    }
}
