//! WebSocket transport implementation

use super::{Connection, Transport};
use std::io;
use std::pin::Pin;
use std::task::{Context, Poll};
use tokio::io::{AsyncRead, AsyncWrite, ReadBuf};
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_async, WebSocketStream};
use futures_util::{SinkExt, StreamExt};
use tokio_tungstenite::tungstenite::Message;

/// WebSocket transport
pub struct WebSocketTransport {
    listener: TcpListener,
}

impl Transport for WebSocketTransport {
    type Connection = WebSocketConnection;

    async fn bind(address: &str) -> io::Result<Self> {
        let listener = TcpListener::bind(address).await?;
        Ok(Self { listener })
    }

    async fn accept(&self) -> io::Result<WebSocketConnection> {
        let (stream, _addr) = self.listener.accept().await?;
        stream.set_nodelay(true)?;

        // Perform WebSocket handshake
        let ws_stream = accept_async(stream)
            .await
            .map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;

        Ok(WebSocketConnection::new(ws_stream))
    }

    fn local_addr(&self) -> io::Result<String> {
        Ok(self.listener.local_addr()?.to_string())
    }
}

/// WebSocket connection wrapper
/// 
/// Converts between WebSocket frames and raw bytes for protocol compatibility
pub struct WebSocketConnection {
    ws: WebSocketStream<TcpStream>,
    /// Buffer for incoming data extracted from WebSocket frames
    read_buffer: Vec<u8>,
    /// Position in read buffer
    read_pos: usize,
    open: bool,
}

impl WebSocketConnection {
    pub fn new(ws: WebSocketStream<TcpStream>) -> Self {
        Self {
            ws,
            read_buffer: Vec::new(),
            read_pos: 0,
            open: true,
        }
    }

    /// Send binary data as a WebSocket frame
    pub async fn send_binary(&mut self, data: &[u8]) -> io::Result<()> {
        if !self.open {
            return Err(io::Error::new(io::ErrorKind::NotConnected, "Connection closed"));
        }

        self.ws
            .send(Message::Binary(data.to_vec().into()))
            .await
            .map_err(|e| io::Error::new(io::ErrorKind::Other, e))
    }

    /// Receive binary data from WebSocket frame
    pub async fn recv_binary(&mut self) -> io::Result<Option<Vec<u8>>> {
        if !self.open {
            return Err(io::Error::new(io::ErrorKind::NotConnected, "Connection closed"));
        }

        match self.ws.next().await {
            Some(Ok(Message::Binary(data))) => Ok(Some(data.to_vec())),
            Some(Ok(Message::Close(_))) => {
                self.open = false;
                Ok(None)
            }
            Some(Ok(_)) => Ok(None), // Ignore text, ping, pong
            Some(Err(e)) => {
                self.open = false;
                Err(io::Error::new(io::ErrorKind::Other, e))
            }
            None => {
                self.open = false;
                Ok(None)
            }
        }
    }
}

impl Connection for WebSocketConnection {
    fn close(&mut self) {
        self.open = false;
    }

    fn is_open(&self) -> bool {
        self.open
    }
}

// AsyncRead/AsyncWrite impl for WebSocket is complex due to framing.
// We provide higher-level send_binary/recv_binary instead.
// For now, implement stubs that will be replaced with proper buffering.

impl AsyncRead for WebSocketConnection {
    fn poll_read(
        mut self: Pin<&mut Self>,
        _cx: &mut Context<'_>,
        buf: &mut ReadBuf<'_>,
    ) -> Poll<io::Result<()>> {
        // Return buffered data if available
        if self.read_pos < self.read_buffer.len() {
            let available = &self.read_buffer[self.read_pos..];
            let to_copy = available.len().min(buf.remaining());
            buf.put_slice(&available[..to_copy]);
            self.read_pos += to_copy;
            
            // Clear buffer if fully consumed
            if self.read_pos >= self.read_buffer.len() {
                self.read_buffer.clear();
                self.read_pos = 0;
            }
            
            return Poll::Ready(Ok(()));
        }

        // For actual async reading, use recv_binary() instead
        // This is a simplified implementation
        Poll::Pending
    }
}

impl AsyncWrite for WebSocketConnection {
    fn poll_write(
        self: Pin<&mut Self>,
        _cx: &mut Context<'_>,
        _buf: &[u8],
    ) -> Poll<io::Result<usize>> {
        // For actual async writing, use send_binary() instead
        Poll::Pending
    }

    fn poll_flush(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Poll::Ready(Ok(()))
    }

    fn poll_shutdown(mut self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        self.open = false;
        Poll::Ready(Ok(()))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio_tungstenite::connect_async;

    #[tokio::test]
    async fn test_websocket_bind() {
        let transport = WebSocketTransport::bind("127.0.0.1:0").await.unwrap();
        let addr = transport.local_addr().unwrap();
        assert!(addr.starts_with("127.0.0.1:"));
    }

    #[tokio::test]
    async fn test_websocket_connect_and_send() {
        let transport = WebSocketTransport::bind("127.0.0.1:0").await.unwrap();
        let addr = transport.local_addr().unwrap();

        // Client connects with WebSocket
        let client_handle = tokio::spawn(async move {
            let url = format!("ws://{}", addr);
            let (mut ws, _) = connect_async(&url).await.unwrap();
            
            // Send binary message
            ws.send(Message::Binary(b"hello from client".to_vec().into()))
                .await
                .unwrap();

            // Receive response
            let msg = ws.next().await.unwrap().unwrap();
            if let Message::Binary(data) = msg {
                assert_eq!(&data[..], b"hello from server");
            } else {
                panic!("Expected binary message");
            }
        });

        // Server accepts
        let mut conn = transport.accept().await.unwrap();
        assert!(conn.is_open());

        // Receive from client
        let data = conn.recv_binary().await.unwrap().unwrap();
        assert_eq!(data, b"hello from client");

        // Send response
        conn.send_binary(b"hello from server").await.unwrap();

        client_handle.await.unwrap();
    }

    #[tokio::test]
    async fn test_websocket_connection_close() {
        let transport = WebSocketTransport::bind("127.0.0.1:0").await.unwrap();
        let addr = transport.local_addr().unwrap();

        tokio::spawn(async move {
            let url = format!("ws://{}", addr);
            let (ws, _) = connect_async(&url).await.unwrap();
            drop(ws); // Close client
        });

        let mut conn = transport.accept().await.unwrap();
        assert!(conn.is_open());
        conn.close();
        assert!(!conn.is_open());
    }
}
