//! Perun Server - Rust Implementation
//!
//! Display server for emulators that speaks the Perun protocol.

use std::sync::Arc;
use std::sync::atomic::{AtomicU32, Ordering};
use clap::Parser;
use tokio::net::TcpListener;
use tokio::sync::broadcast;
use tracing::{info, error, warn, debug, Level};
use tracing_subscriber::FmtSubscriber;

use perun_server::{
    Server, ServerConfig, ServerEvent, BroadcastMessage,
    transport::{tcp::TcpTransport, websocket::{WebSocketTransport, WebSocketConnection}, Transport},
    protocol::{capabilities, Handshake, PacketHeader, PacketType, InputEventPacket},
};

static WS_CLIENT_ID: AtomicU32 = AtomicU32::new(10000); // Start WS clients at 10000

/// Handle a WebSocket client connection with full Perun protocol support
async fn handle_websocket_client(
    _server: &Arc<Server>,
    conn: &mut WebSocketConnection,
    broadcast_tx: broadcast::Sender<BroadcastMessage>,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let client_id = WS_CLIENT_ID.fetch_add(1, Ordering::SeqCst);
    
    // 1. Handshake: receive HELLO
    let hello_data = conn.recv_binary().await?
        .ok_or_else(|| "Connection closed during handshake")?;
    
    let server_caps = capabilities::CAP_DELTA | capabilities::CAP_AUDIO | capabilities::CAP_DEBUG;
    let result = Handshake::process_hello(&hello_data, server_caps)?;
    
    if !result.accepted {
        let error_msg = result.error.unwrap_or_else(|| "Unknown error".to_string());
        let error_resp = Handshake::create_error(&error_msg);
        conn.send_binary(&error_resp).await?;
        return Err(error_msg.into());
    }
    
    // 2. Send OK response
    let ok_resp = Handshake::create_ok(result.version, result.capabilities);
    conn.send_binary(&ok_resp).await?;
    
    info!("WebSocket client {} handshake complete, caps: 0x{:04x}", client_id, result.capabilities);
    
    // 3. Subscribe to broadcasts
    let mut broadcast_rx = broadcast_tx.subscribe();
    
    // 4. Main loop
    loop {
        tokio::select! {
            // Receive from WebSocket client
            recv_result = conn.recv_binary() => {
                match recv_result {
                    Ok(Some(data)) => {
                        if data.len() >= PacketHeader::SIZE {
                            if let Ok(header) = PacketHeader::deserialize(&data) {
                                debug!("WS client {} packet: {:?}", client_id, header.packet_type);
                                // Broadcast input events to emulator
                                if header.packet_type == PacketType::InputEvent {
                                    if let Ok(input) = InputEventPacket::deserialize(&data[PacketHeader::SIZE..]) {
                                        debug!("WS client {} input: buttons=0x{:04x}", client_id, input.buttons);
                                        let _ = broadcast_tx.send(BroadcastMessage::InputEvent {
                                            packet: input,
                                            exclude_client: Some(client_id),
                                        });
                                    }
                                }
                            }
                        }
                    }
                    Ok(None) => {
                        debug!("WebSocket client {} disconnected cleanly", client_id);
                        break;
                    }
                    Err(e) => {
                        debug!("WebSocket client {} recv error: {:?}", client_id, e);
                        break;
                    }
                }
            }
            
            // Send broadcasts to this WebSocket client
            broadcast_result = broadcast_rx.recv() => {
                match broadcast_result {
                    Ok(msg) => {
                        let (packet_type, payload, exclude) = match msg {
                            BroadcastMessage::VideoFrame { packet, exclude_client } => {
                                (PacketType::VideoFrame, packet.serialize(), exclude_client)
                            }
                            BroadcastMessage::AudioChunk { packet, exclude_client } => {
                                (PacketType::AudioChunk, packet.serialize(), exclude_client)
                            }
                            BroadcastMessage::InputEvent { packet, exclude_client } => {
                                (PacketType::InputEvent, packet.serialize(), exclude_client)
                            }
                        };
                        
                        // Don't send to excluded client
                        if exclude == Some(client_id) {
                            continue;
                        }
                        
                        let header = PacketHeader {
                            packet_type,
                            flags: 0,
                            sequence: 0,
                            length: payload.len() as u32,
                        };
                        
                        let mut data = header.serialize().to_vec();
                        data.extend_from_slice(&payload);
                        
                        if let Err(e) = conn.send_binary(&data).await {
                            warn!("WS client {} send error: {:?}", client_id, e);
                            break;
                        }
                    }
                    Err(broadcast::error::RecvError::Lagged(n)) => {
                        warn!("WS client {} lagged by {} messages", client_id, n);
                    }
                    Err(broadcast::error::RecvError::Closed) => {
                        break;
                    }
                }
            }
        }
    }
    
    info!("WebSocket client {} disconnected", client_id);
    Ok(())
}

/// Perun Display Server (Rust)
#[derive(Parser, Debug)]
#[command(name = "perun-server-rs")]
#[command(about = "Display server for emulators", long_about = None)]
struct Args {
    /// TCP address to listen on (e.g., ":8080" or "0.0.0.0:8080")
    #[arg(long)]
    tcp: Option<String>,

    /// WebSocket address to listen on (e.g., ":8081")
    #[arg(long)]
    ws: Option<String>,

    /// Unix socket path (not yet implemented in Rust version)
    #[arg(long)]
    unix: Option<String>,

    /// Enable debug logging
    #[arg(short, long)]
    debug: bool,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args = Args::parse();

    // Initialize logging
    let subscriber = FmtSubscriber::builder()
        .with_max_level(if args.debug { Level::DEBUG } else { Level::INFO })
        .finish();
    tracing::subscriber::set_global_default(subscriber)?;

    info!("Perun Server (Rust) starting...");

    // Create server
    let config = ServerConfig {
        capabilities: capabilities::CAP_DELTA | capabilities::CAP_AUDIO | capabilities::CAP_DEBUG,
        ..Default::default()
    };
    let (server, mut handle) = Server::with_config(config);
    let server = Arc::new(server);

    // Start transports
    if let Some(tcp_addr) = &args.tcp {
        let addr = if tcp_addr.starts_with(':') {
            format!("0.0.0.0{}", tcp_addr)
        } else {
            tcp_addr.clone()
        };

        let listener = TcpListener::bind(&addr).await?;
        info!("TCP transport listening on {}", addr);

        let server_clone = Arc::clone(&server);
        tokio::spawn(async move {
            loop {
                match listener.accept().await {
                    Ok((stream, peer)) => {
                        info!("TCP connection from {}", peer);
                        stream.set_nodelay(true).ok();
                        
                        let server = Arc::clone(&server_clone);
                        tokio::spawn(async move {
                            if let Err(e) = server.handle_client(stream).await {
                                error!("Client error: {:?}", e);
                            }
                        });
                    }
                    Err(e) => {
                        error!("Accept error: {}", e);
                    }
                }
            }
        });
    }

    if let Some(ws_addr) = &args.ws {
        let addr = if ws_addr.starts_with(':') {
            format!("0.0.0.0{}", ws_addr)
        } else {
            ws_addr.clone()
        };

        let transport = WebSocketTransport::bind(&addr).await?;
        info!("WebSocket transport listening on {}", transport.local_addr()?);

        let server_clone = Arc::clone(&server);
        tokio::spawn(async move {
            loop {
                match transport.accept().await {
                    Ok(mut conn) => {
                        info!("WebSocket connection accepted");
                        
                        let server = Arc::clone(&server_clone);
                        tokio::spawn(async move {
                            // Handle WebSocket client with full protocol support
                            let broadcast_tx = server.broadcast_sender();
                            if let Err(e) = handle_websocket_client(&server, &mut conn, broadcast_tx).await {
                                error!("WebSocket client error: {:?}", e);
                            }
                        });
                    }
                    Err(e) => {
                        error!("WebSocket accept error: {}", e);
                    }
                }
            }
        });
    }

    if args.unix.is_some() {
        info!("Unix socket not yet implemented in Rust version");
    }

    if args.tcp.is_none() && args.ws.is_none() && args.unix.is_none() {
        error!("No transport configured! Use --tcp, --ws, or --unix");
        std::process::exit(1);
    }

    // Event loop - process server events
    info!("Server running. Press Ctrl+C to stop.");
    
    // Take ownership of event receiver
    if let Some(mut event_rx) = handle.event_rx.take() {
        loop {
            tokio::select! {
                Some(event) = event_rx.recv() => {
                    match event {
                        ServerEvent::ClientConnected { id, capabilities } => {
                            info!("Client {} connected, caps: 0x{:04x}", id, capabilities);
                        }
                        ServerEvent::ClientDisconnected { id } => {
                            info!("Client {} disconnected", id);
                        }
                        ServerEvent::VideoFrameReceived { client_id, packet } => {
                            // Broadcast to all other clients
                            handle.broadcast_video_frame(packet, Some(client_id));
                        }
                        ServerEvent::AudioChunkReceived { client_id, packet } => {
                            handle.broadcast_audio_chunk(packet, Some(client_id));
                        }
                        ServerEvent::InputEventReceived { client_id, packet } => {
                            handle.broadcast_input_event(packet, Some(client_id));
                        }
                        ServerEvent::ConfigReceived { client_id, data } => {
                            info!("Config from client {}: {} bytes", client_id, data.len());
                        }
                    }
                }
                _ = tokio::signal::ctrl_c() => {
                    info!("Shutting down...");
                    break;
                }
            }
        }
    }

    Ok(())
}
