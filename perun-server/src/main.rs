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
};
use perun_protocol::{capabilities, Handshake, PacketHeader, PacketType, VideoFramePacket, InputEventPacket};
mod shm;


static WS_CLIENT_ID: AtomicU32 = AtomicU32::new(10000); // Start WS clients at 10000


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

    /// Shared Memory path (e.g., /dev/shm/perun)
    #[arg(long)]
    shm: Option<String>,

    /// SHM Width (default 256)
    #[arg(long, default_value_t = 256)]
    width: u32,

    /// SHM Height (default 224)
    #[arg(long, default_value_t = 224)]
    height: u32,
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
                            if let Err(e) = server.handle_client(conn).await {
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

    // Initialize SHM if configured
    let shm_host_arc = if let Some(shm_path) = args.shm {
        info!("Initializing SHM host at {} ({}x{})", shm_path, args.width, args.height);
        match shm::ShmHost::new(&shm_path, args.width, args.height) {
            Ok(shm_host) => {
                let shm_host = Arc::new(shm_host);
                let shm_host_clone = shm_host.clone();
                let handle = handle.clone_sender();
                let width = args.width;
                let height = args.height;
                
                // Spawn blocking thread for SHM polling
                std::thread::spawn(move || {
                    let mut buffer = Vec::new();
                    let mut processor = perun_server::FrameProcessor::new();
                    info!("SHM polling thread started");
                    loop {
                        if let Some((w, h)) = shm_host_clone.read_frame_into(&mut buffer) {
                            // Process frame (Delta + Compression)
                            let (packet, flags) = processor.process(w as u16, h as u16, &buffer);
                            
                            // Send to broadcast
                            // Note: packet.data is ALREADY compressed by processor.
                            // We need to ensure logic downstream handles this.
                            // The server handle just forwards packet.
                            // But `server.rs` calculates flags again?
                            // No, `BroadcastMessage` carries the packet.
                            // We need to pass the flags too? 
                            // `BroadcastMessage` just has the packet and exclude_client.
                            // The `server.rs` reconstructs headers.
                            // We need `packet.is_delta` to be correct (it is).
                            
                            // count frames for debug
                            // static FRAME_COUNT: AtomicU32 = AtomicU32::new(0); // Cannot use static in closure
                            // ignoring count for now, just log periodically if needed
                            // info!("Broadcasting frame");
                            
                            handle.broadcast_video_frame(packet, None); 
                        } else {
                            std::thread::sleep(std::time::Duration::from_micros(500));
                        }
                    }
                });
                Some(shm_host)
            }
            Err(e) => {
                error!("Failed to initialize SHM: {}", e);
                None
            }
        }
    } else {
        None
    };

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
                            info!("Input packet from {}: buttons=0x{:04x}, res=0x{:04x}", client_id, packet.buttons, packet.reserved);
                            if let Some(shm) = &shm_host_arc {
                                shm.write_inputs(packet.buttons);
                            }
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
