//! Server core implementation
//!
//! Manages client connections, protocol handling, and broadcasting.

use crate::protocol::{
    capabilities, Handshake, HandshakeResult, PacketHeader, PacketType, ProtocolError,
    VideoFramePacket, AudioChunkPacket, InputEventPacket,
};
use std::collections::HashMap;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::sync::{broadcast, mpsc, RwLock};
use tracing::{debug, error, info, warn};

/// Unique client identifier
pub type ClientId = u32;

/// Server configuration
#[derive(Debug, Clone)]
pub struct ServerConfig {
    /// Capabilities this server supports
    pub capabilities: u16,
    /// Maximum clients
    pub max_clients: usize,
    /// Broadcast channel buffer size
    pub broadcast_buffer: usize,
}

impl Default for ServerConfig {
    fn default() -> Self {
        Self {
            capabilities: capabilities::CAP_DELTA | capabilities::CAP_AUDIO | capabilities::CAP_DEBUG,
            max_clients: 100,
            broadcast_buffer: 16,
        }
    }
}

/// Client state
#[derive(Debug)]
pub struct ClientState {
    pub id: ClientId,
    pub capabilities: u16,
    pub handshake_complete: bool,
}

/// Server event for callbacks
#[derive(Debug, Clone)]
pub enum ServerEvent {
    ClientConnected { id: ClientId, capabilities: u16 },
    ClientDisconnected { id: ClientId },
    VideoFrameReceived { client_id: ClientId, packet: VideoFramePacket },
    AudioChunkReceived { client_id: ClientId, packet: AudioChunkPacket },
    InputEventReceived { client_id: ClientId, packet: InputEventPacket },
    ConfigReceived { client_id: ClientId, data: Vec<u8> },
}

/// Broadcast message (sent to all clients)
#[derive(Debug, Clone)]
pub enum BroadcastMessage {
    VideoFrame { packet: VideoFramePacket, exclude_client: Option<ClientId> },
    AudioChunk { packet: AudioChunkPacket, exclude_client: Option<ClientId> },
    InputEvent { packet: InputEventPacket, exclude_client: Option<ClientId> },
}

/// Server handle for sending commands
pub struct ServerHandle {
    broadcast_tx: broadcast::Sender<BroadcastMessage>,
    pub event_rx: Option<mpsc::Receiver<ServerEvent>>,
}

impl ServerHandle {
    /// Broadcast a video frame to all clients
    pub fn broadcast_video_frame(&self, packet: VideoFramePacket, exclude_client: Option<ClientId>) {
        let _ = self.broadcast_tx.send(BroadcastMessage::VideoFrame { packet, exclude_client });
    }

    /// Broadcast an audio chunk to all clients
    pub fn broadcast_audio_chunk(&self, packet: AudioChunkPacket, exclude_client: Option<ClientId>) {
        let _ = self.broadcast_tx.send(BroadcastMessage::AudioChunk { packet, exclude_client });
    }

    /// Broadcast an input event to all clients
    pub fn broadcast_input_event(&self, packet: InputEventPacket, exclude_client: Option<ClientId>) {
        let _ = self.broadcast_tx.send(BroadcastMessage::InputEvent { packet, exclude_client });
    }
}

/// Server core
pub struct Server {
    config: ServerConfig,
    clients: Arc<RwLock<HashMap<ClientId, ClientState>>>,
    next_client_id: AtomicU32,
    broadcast_tx: broadcast::Sender<BroadcastMessage>,
    event_tx: mpsc::Sender<ServerEvent>,
}

impl Server {
    /// Create a new server with default config
    pub fn new() -> (Self, ServerHandle) {
        Self::with_config(ServerConfig::default())
    }

    /// Create a new server with custom config
    pub fn with_config(config: ServerConfig) -> (Self, ServerHandle) {
        let (broadcast_tx, _) = broadcast::channel(config.broadcast_buffer);
        let (event_tx, event_rx) = mpsc::channel(100);

        let server = Self {
            config,
            clients: Arc::new(RwLock::new(HashMap::new())),
            next_client_id: AtomicU32::new(1),
            broadcast_tx: broadcast_tx.clone(),
            event_tx,
        };

        let handle = ServerHandle {
            broadcast_tx,
            event_rx: Some(event_rx),
        };

        (server, handle)
    }

    /// Get the number of connected clients
    pub async fn client_count(&self) -> usize {
        self.clients.read().await.len()
    }

    /// Process a client connection (runs until disconnect)
    pub async fn handle_client<C>(&self, mut conn: C) -> Result<(), ProtocolError>
    where
        C: AsyncReadExt + AsyncWriteExt + Unpin + Send,
    {
        let client_id = self.next_client_id.fetch_add(1, Ordering::SeqCst);
        info!("New connection, client ID: {}", client_id);

        // Handshake phase
        let mut handshake_buf = vec![0u8; 256];
        let n = conn.read(&mut handshake_buf).await.map_err(|_| ProtocolError::InvalidData)?;
        
        if n < 15 {
            let error_resp = Handshake::create_error("Incomplete handshake");
            let _ = conn.write_all(&error_resp).await;
            return Err(ProtocolError::BufferTooSmall { needed: 15, have: n });
        }

        let result = Handshake::process_hello(&handshake_buf[..n], self.config.capabilities)?;
        
        if !result.accepted {
            let error_msg = result.error.unwrap_or_else(|| "Unknown error".to_string());
            let error_resp = Handshake::create_error(&error_msg);
            let _ = conn.write_all(&error_resp).await;
            return Err(ProtocolError::InvalidData);
        }

        // Send OK response
        let ok_resp = Handshake::create_ok(1, result.capabilities);
        conn.write_all(&ok_resp).await.map_err(|_| ProtocolError::InvalidData)?;

        info!("Client {} handshake complete, caps: 0x{:04x}", client_id, result.capabilities);

        // Register client
        let client_state = ClientState {
            id: client_id,
            capabilities: result.capabilities,
            handshake_complete: true,
        };
        self.clients.write().await.insert(client_id, client_state);

        // Notify connected
        let _ = self.event_tx.send(ServerEvent::ClientConnected {
            id: client_id,
            capabilities: result.capabilities,
        }).await;

        // Subscribe to broadcasts
        let mut broadcast_rx = self.broadcast_tx.subscribe();

        // Main receive loop
        let mut recv_buf = vec![0u8; 65536];
        let mut pending_data = Vec::new();

        loop {
            tokio::select! {
                // Receive from client
                read_result = conn.read(&mut recv_buf) => {
                    match read_result {
                        Ok(0) => {
                            debug!("Client {} disconnected (EOF)", client_id);
                            break;
                        }
                        Ok(n) => {
                            pending_data.extend_from_slice(&recv_buf[..n]);
                            
                            // Process complete packets
                            while pending_data.len() >= PacketHeader::SIZE {
                                let header = match PacketHeader::deserialize(&pending_data) {
                                    Ok(h) => h,
                                    Err(_) => break,
                                };
                                
                                let total_len = PacketHeader::SIZE + header.length as usize;
                                if pending_data.len() < total_len {
                                    break;
                                }
                                
                                let payload = &pending_data[PacketHeader::SIZE..total_len];
                                self.handle_packet(client_id, &header, payload).await;
                                
                                pending_data.drain(..total_len);
                            }
                        }
                        Err(e) => {
                            warn!("Client {} read error: {}", client_id, e);
                            break;
                        }
                    }
                }
                
                // Send broadcasts to this client
                broadcast_result = broadcast_rx.recv() => {
                    match broadcast_result {
                        Ok(msg) => {
                            if let Err(e) = self.send_broadcast(&mut conn, client_id, msg).await {
                                warn!("Client {} send error: {:?}", client_id, e);
                                break;
                            }
                        }
                        Err(broadcast::error::RecvError::Lagged(n)) => {
                            warn!("Client {} lagged by {} messages", client_id, n);
                        }
                        Err(broadcast::error::RecvError::Closed) => {
                            break;
                        }
                    }
                }
            }
        }

        // Cleanup
        self.clients.write().await.remove(&client_id);
        let _ = self.event_tx.send(ServerEvent::ClientDisconnected { id: client_id }).await;
        info!("Client {} disconnected", client_id);

        Ok(())
    }

    async fn handle_packet(&self, client_id: ClientId, header: &PacketHeader, payload: &[u8]) {
        match header.packet_type {
            PacketType::VideoFrame => {
                match VideoFramePacket::deserialize(payload, header.flags) {
                    Ok(packet) => {
                        let _ = self.event_tx.send(ServerEvent::VideoFrameReceived {
                            client_id,
                            packet,
                        }).await;
                    }
                    Err(e) => warn!("Client {} malformed VideoFrame: {}", client_id, e),
                }
            }
            PacketType::AudioChunk => {
                match AudioChunkPacket::deserialize(payload) {
                    Ok(packet) => {
                        let _ = self.event_tx.send(ServerEvent::AudioChunkReceived {
                            client_id,
                            packet,
                        }).await;
                    }
                    Err(e) => warn!("Client {} malformed AudioChunk: {}", client_id, e),
                }
            }
            PacketType::InputEvent => {
                match InputEventPacket::deserialize(payload) {
                    Ok(packet) => {
                        let _ = self.event_tx.send(ServerEvent::InputEventReceived {
                            client_id,
                            packet,
                        }).await;
                    }
                    Err(e) => warn!("Client {} malformed InputEvent: {}", client_id, e),
                }
            }
            PacketType::Config => {
                let _ = self.event_tx.send(ServerEvent::ConfigReceived {
                    client_id,
                    data: payload.to_vec(),
                }).await;
            }
            PacketType::DebugInfo => {
                debug!("Received debug info from client {}", client_id);
            }
        }
    }

    async fn send_broadcast<C>(
        &self,
        conn: &mut C,
        client_id: ClientId,
        msg: BroadcastMessage,
    ) -> Result<(), ProtocolError>
    where
        C: AsyncWriteExt + Unpin,
    {
        let (packet_type, payload, exclude) = match &msg {
            BroadcastMessage::VideoFrame { packet, exclude_client } => {
                (PacketType::VideoFrame, packet.serialize(), *exclude_client)
            }
            BroadcastMessage::AudioChunk { packet, exclude_client } => {
                (PacketType::AudioChunk, packet.serialize(), *exclude_client)
            }
            BroadcastMessage::InputEvent { packet, exclude_client } => {
                (PacketType::InputEvent, packet.serialize(), *exclude_client)
            }
        };

        // Check if this client should be excluded
        if exclude == Some(client_id) {
            return Ok(());
        }

        let header = PacketHeader {
            packet_type,
            flags: if let BroadcastMessage::VideoFrame { packet, .. } = &msg {
                if packet.is_delta {
                    crate::protocol::flags::FLAG_DELTA
                } else {
                    0
                }
            } else {
                0
            },
            sequence: 0, // TODO: per-client sequence tracking
            length: payload.len() as u32,
        };

        let mut data = header.serialize().to_vec();
        data.extend_from_slice(&payload);

        conn.write_all(&data).await.map_err(|_| ProtocolError::InvalidData)
    }
    /// Get a reference to the broadcast sender
    pub fn broadcast_sender(&self) -> broadcast::Sender<BroadcastMessage> {
        self.broadcast_tx.clone()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio::io::duplex;

    #[tokio::test]
    async fn test_server_creation() {
        let (server, _handle) = Server::new();
        assert_eq!(server.client_count().await, 0);
    }

    #[tokio::test]
    async fn test_client_handshake() {
        let (server, _handle) = Server::new();
        let (mut client, server_conn) = duplex(4096);

        // Spawn server handler
        let server_handle = tokio::spawn(async move {
            server.handle_client(server_conn).await
        });

        // Client sends HELLO
        let hello = Handshake::create_hello(1, capabilities::CAP_DELTA);
        client.write_all(&hello).await.unwrap();

        // Client receives OK
        let mut response = vec![0u8; 256];
        let n = client.read(&mut response).await.unwrap();
        
        let result = Handshake::process_response(&response[..n]).unwrap();
        assert!(result.accepted);
        assert_eq!(result.capabilities, capabilities::CAP_DELTA);

        // Close client to end test
        drop(client);
        
        // Server should complete
        let _ = server_handle.await;
    }

    #[tokio::test]
    async fn test_client_count() {
        let (server, _handle) = Server::new();
        let server = Arc::new(server);
        
        let (mut client, server_conn) = duplex(4096);
        
        let server_clone = Arc::clone(&server);
        let server_handle = tokio::spawn(async move {
            server_clone.handle_client(server_conn).await
        });

        // Send handshake
        let hello = Handshake::create_hello(1, capabilities::CAP_DELTA);
        client.write_all(&hello).await.unwrap();

        // Wait for handshake response
        let mut response = vec![0u8; 256];
        let _ = client.read(&mut response).await.unwrap();

        // Small delay to let server register client
        tokio::time::sleep(tokio::time::Duration::from_millis(10)).await;

        assert_eq!(server.client_count().await, 1);

        // Disconnect
        drop(client);
        let _ = server_handle.await;

        // Client should be removed
        tokio::time::sleep(tokio::time::Duration::from_millis(10)).await;
        assert_eq!(server.client_count().await, 0);
    }

    #[tokio::test]
    async fn test_broadcast_video_frame() {
        let (server, handle) = Server::new();
        let server = Arc::new(server);
        
        let (mut client, server_conn) = duplex(4096);
        
        let server_clone = Arc::clone(&server);
        let _server_handle = tokio::spawn(async move {
            server_clone.handle_client(server_conn).await
        });

        // Complete handshake
        let hello = Handshake::create_hello(1, capabilities::CAP_DELTA);
        client.write_all(&hello).await.unwrap();
        
        let mut response = vec![0u8; 256];
        let _ = client.read(&mut response).await.unwrap();

        // Broadcast a frame
        let frame = VideoFramePacket {
            width: 64,
            height: 32,
            is_delta: false,
            data: vec![0xFF; 100],
        };
        handle.broadcast_video_frame(frame, None);

        // Client should receive it
        let mut data = vec![0u8; 256];
        let n = client.read(&mut data).await.unwrap();
        
        assert!(n > PacketHeader::SIZE);
        let header = PacketHeader::deserialize(&data).unwrap();
        assert_eq!(header.packet_type, PacketType::VideoFrame);
    }
}
