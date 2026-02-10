//! Transport layer abstraction
//!
//! Provides async traits for different transport types (TCP, WebSocket, etc.)

pub mod tcp;
pub mod websocket;

use std::io;
use tokio::io::{AsyncRead, AsyncWrite};

/// A transport that can accept incoming connections
#[allow(async_fn_in_trait)]
pub trait Transport: Send + Sync {
    type Connection: Connection;

    /// Start listening on the given address
    async fn bind(address: &str) -> io::Result<Self>
    where
        Self: Sized;

    /// Accept a new connection (non-blocking, returns None if no connection pending)
    async fn accept(&self) -> io::Result<Self::Connection>;

    /// Get the local address being listened on
    fn local_addr(&self) -> io::Result<String>;
}

/// A bidirectional connection
pub trait Connection: AsyncRead + AsyncWrite + Send + Sync + Unpin {
    /// Close the connection
    fn close(&mut self);

    /// Check if connection is still open
    fn is_open(&self) -> bool;
}
