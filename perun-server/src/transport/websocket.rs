//! WebSocket transport implementation

use super::{Connection, Transport};
use std::io;
use std::pin::Pin;
use std::task::{Context, Poll};
use tokio::io::{AsyncRead, AsyncWrite, ReadBuf};
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::{accept_async, WebSocketStream};
use futures_util::{SinkExt, StreamExt, Stream, Sink};
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
    /// Buffer for outgoing data to be sent as WebSocket frames
    write_buffer: Vec<u8>,
    open: bool,
}

impl WebSocketConnection {
    pub fn new(ws: WebSocketStream<TcpStream>) -> Self {
        Self {
            ws,
            read_buffer: Vec::new(),
            read_pos: 0,
            write_buffer: Vec::new(),
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
        cx: &mut Context<'_>,
        buf: &mut ReadBuf<'_>,
    ) -> Poll<io::Result<()>> {
        // println!("[WS] poll_read called, pos={}, len={}", self.read_pos, self.read_buffer.len());
        // 1. If we have data in the buffer, return it
        if self.read_pos < self.read_buffer.len() {
            let available = &self.read_buffer[self.read_pos..];
            let to_copy = available.len().min(buf.remaining());
            buf.put_slice(&available[..to_copy]);
            self.read_pos += to_copy;
            
            // println!("[WS] Returning {} bytes from buffer", to_copy);
            
            if self.read_pos >= self.read_buffer.len() {
                self.read_buffer.clear();
                self.read_pos = 0;
            }
            
            return Poll::Ready(Ok(()));
        }

        // 2. No data in buffer, poll the WebSocket stream
        match Pin::new(&mut self.ws).poll_next(cx) {
            Poll::Ready(Some(Ok(Message::Binary(data)))) => {
                println!("[WS] Received binary message, len={}", data.len());
                self.read_buffer = data.to_vec();
                self.read_pos = 0;
                
                // Now we have data, call ourselves again for the copy logic
                // We use self.as_mut().poll_read to avoid move issues if any
                self.as_mut().poll_read(cx, buf)
            }
            Poll::Ready(Some(Ok(Message::Close(_)))) | Poll::Ready(None) => {
                println!("[WS] Stream closed");
                self.open = false;
                Poll::Ready(Ok(())) // EOF
            }
            Poll::Ready(Some(Ok(m))) => {
                println!("[WS] Received non-binary message: {:?}", m);
                cx.waker().wake_by_ref();
                Poll::Pending
            }
            Poll::Ready(Some(Err(e))) => {
                println!("[WS] Stream error: {:?}", e);
                Poll::Ready(Err(io::Error::new(io::ErrorKind::Other, e)))
            }
            Poll::Pending => Poll::Pending,
        }
    }
}

impl AsyncWrite for WebSocketConnection {
    fn poll_write(
        mut self: Pin<&mut Self>,
        _cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<io::Result<usize>> {
        if !self.open {
            return Poll::Ready(Err(io::Error::new(io::ErrorKind::NotConnected, "Connection closed")));
        }
        
        // Just append to write buffer
        self.write_buffer.extend_from_slice(buf);
        Poll::Ready(Ok(buf.len()))
    }

    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        if self.write_buffer.is_empty() {
            return Pin::new(&mut self.ws).poll_flush(cx).map_err(|e| io::Error::new(io::ErrorKind::Other, e));
        }

        // Try to send the buffer as a binary frame
        // We need to use ready! or handle Pending
        match Pin::new(&mut self.ws).poll_ready(cx) {
            Poll::Ready(Ok(())) => {
                let data = std::mem::take(&mut self.write_buffer);
                match Pin::new(&mut self.ws).start_send(Message::Binary(data.into())) {
                    Ok(()) => {
                        // After start_send, we should poll_flush the underlying sink
                        Pin::new(&mut self.ws).poll_flush(cx).map_err(|e| io::Error::new(io::ErrorKind::Other, e))
                    }
                    Err(e) => Poll::Ready(Err(io::Error::new(io::ErrorKind::Other, e))),
                }
            }
            Poll::Ready(Err(e)) => Poll::Ready(Err(io::Error::new(io::ErrorKind::Other, e))),
            Poll::Pending => Poll::Pending,
        }
    }

    fn poll_shutdown(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        // Attempt to flush last data
        if !self.write_buffer.is_empty() {
            let _ = self.as_mut().poll_flush(cx);
        }
        self.open = false;
        Pin::new(&mut self.ws).poll_close(cx).map_err(|e| io::Error::new(io::ErrorKind::Other, e))
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
