//! TCP transport implementation

use super::{Connection, Transport};
use std::io;
use std::pin::Pin;
use std::task::{Context, Poll};
use tokio::io::{AsyncRead, AsyncWrite, ReadBuf};
use tokio::net::{TcpListener, TcpStream};

/// TCP transport
pub struct TcpTransport {
    listener: TcpListener,
}

impl Transport for TcpTransport {
    type Connection = TcpConnection;

    async fn bind(address: &str) -> io::Result<Self> {
        let listener = TcpListener::bind(address).await?;
        
        // Set TCP_NODELAY on the listener is not possible, 
        // we do it per-connection in accept()
        
        Ok(Self { listener })
    }

    async fn accept(&self) -> io::Result<TcpConnection> {
        let (stream, _addr) = self.listener.accept().await?;
        
        // Disable Nagle's algorithm for low latency
        stream.set_nodelay(true)?;
        
        Ok(TcpConnection::new(stream))
    }

    fn local_addr(&self) -> io::Result<String> {
        Ok(self.listener.local_addr()?.to_string())
    }
}

/// TCP connection wrapper
pub struct TcpConnection {
    stream: TcpStream,
    open: bool,
}

impl TcpConnection {
    pub fn new(stream: TcpStream) -> Self {
        Self { stream, open: true }
    }
}

impl Connection for TcpConnection {
    fn close(&mut self) {
        self.open = false;
        // TcpStream is closed when dropped
    }

    fn is_open(&self) -> bool {
        self.open
    }
}

impl AsyncRead for TcpConnection {
    fn poll_read(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut ReadBuf<'_>,
    ) -> Poll<io::Result<()>> {
        if !self.open {
            return Poll::Ready(Err(io::Error::new(
                io::ErrorKind::NotConnected,
                "Connection closed",
            )));
        }
        Pin::new(&mut self.stream).poll_read(cx, buf)
    }
}

impl AsyncWrite for TcpConnection {
    fn poll_write(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<io::Result<usize>> {
        if !self.open {
            return Poll::Ready(Err(io::Error::new(
                io::ErrorKind::NotConnected,
                "Connection closed",
            )));
        }
        Pin::new(&mut self.stream).poll_write(cx, buf)
    }

    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Pin::new(&mut self.stream).poll_flush(cx)
    }

    fn poll_shutdown(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        self.open = false;
        Pin::new(&mut self.stream).poll_shutdown(cx)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio::io::{AsyncReadExt, AsyncWriteExt};

    #[tokio::test]
    async fn test_tcp_bind_accept() {
        // Bind to random port
        let transport = TcpTransport::bind("127.0.0.1:0").await.unwrap();
        let addr = transport.local_addr().unwrap();

        // Connect from client side
        let client_handle = tokio::spawn(async move {
            let mut client = TcpStream::connect(&addr).await.unwrap();
            client.write_all(b"hello").await.unwrap();
            
            let mut buf = [0u8; 5];
            client.read_exact(&mut buf).await.unwrap();
            assert_eq!(&buf, b"world");
        });

        // Accept on server side
        let mut conn = transport.accept().await.unwrap();
        assert!(conn.is_open());

        let mut buf = [0u8; 5];
        conn.read_exact(&mut buf).await.unwrap();
        assert_eq!(&buf, b"hello");

        conn.write_all(b"world").await.unwrap();

        client_handle.await.unwrap();
    }

    #[tokio::test]
    async fn test_tcp_connection_close() {
        let transport = TcpTransport::bind("127.0.0.1:0").await.unwrap();
        let addr = transport.local_addr().unwrap();

        let _client = TcpStream::connect(&addr).await.unwrap();
        let mut conn = transport.accept().await.unwrap();

        assert!(conn.is_open());
        conn.close();
        assert!(!conn.is_open());
    }

    #[tokio::test]
    async fn test_tcp_nodelay_enabled() {
        let transport = TcpTransport::bind("127.0.0.1:0").await.unwrap();
        let addr = transport.local_addr().unwrap();

        let _client = TcpStream::connect(&addr).await.unwrap();
        let conn = transport.accept().await.unwrap();

        // Verify TCP_NODELAY is set
        assert!(conn.stream.nodelay().unwrap());
    }
}
