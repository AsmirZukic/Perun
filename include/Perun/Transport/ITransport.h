#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <memory>

namespace Perun::Transport {

// Forward declaration
class IConnection;

/**
 * @brief Callback for when a new connection is accepted
 * @param connection The newly accepted connection
 */
using AcceptCallback = std::function<void(std::shared_ptr<IConnection> connection)>;

/**
 * @brief Callback for when data is received on a connection
 * @param data Pointer to received data
 * @param length Number of bytes received
 */
using ReceiveCallback = std::function<void(const uint8_t* data, size_t length)>;

/**
 * @brief Callback for when a connection is closed
 */
using CloseCallback = std::function<void()>;

/**
 * @brief Interface for a bidirectional connection
 * 
 * Represents a single active connection between client and server.
 * Provides methods for sending/receiving data and managing the connection lifecycle.
 */
class IConnection {
public:
    virtual ~IConnection() = default;
    
    /**
     * @brief Send data over the connection
     * @param data Pointer to data to send
     * @param length Number of bytes to send
     * @param reliable If true, ensures delivery (may block). If false, may drop if buffer full.
     * @return Number of bytes actually sent, 0 if dropped, or -1 on error
     */
    virtual ssize_t Send(const uint8_t* data, size_t length, bool reliable = true) = 0;
    
    /**
     * @brief Receive data from the connection (non-blocking)
     * @param buffer Buffer to store received data
     * @param maxLength Maximum bytes to receive
     * @return Number of bytes received, 0 if would block, -1 on error
     */
    virtual ssize_t Receive(uint8_t* buffer, size_t maxLength) = 0;
    
    /**
     * @brief Close the connection
     */
    virtual void Close() = 0;
    
    /**
     * @brief Check if the connection is still open
     * @return true if connection is active, false otherwise
     */
    virtual bool IsOpen() const = 0;
    
    /**
     * @brief Get the file descriptor for polling (optional)
     * @return File descriptor, or -1 if not applicable
     */
    virtual int GetFileDescriptor() const = 0;
    
    /**
     * @brief Set callback for when data is received
     * @param callback Function to call when data arrives
     */
    virtual void SetReceiveCallback(ReceiveCallback callback) = 0;
    
    /**
     * @brief Set callback for when connection closes
     * @param callback Function to call when connection closes
     */
    virtual void SetCloseCallback(CloseCallback callback) = 0;
};

/**
 * @brief Interface for a transport layer
 * 
 * Provides methods for listening for connections and creating client connections.
 * Different implementations support different protocols (Unix sockets, TCP, WebSocket, etc.)
 */
class ITransport {
public:
    virtual ~ITransport() = default;
    
    /**
     * @brief Start listening for connections
     * @param address Transport-specific address (e.g., "/tmp/perun.sock" for Unix, "0.0.0.0:8080" for TCP)
     * @return true if listening started successfully, false otherwise
     */
    virtual bool Listen(const std::string& address) = 0;
    
    /**
     * @brief Accept a pending connection (non-blocking)
     * @return New connection if available, nullptr if would block or on error
     */
    virtual std::shared_ptr<IConnection> Accept() = 0;
    
    /**
     * @brief Connect to a remote endpoint
     * @param address Transport-specific address
     * @return New connection if successful, nullptr on error
     */
    virtual std::shared_ptr<IConnection> Connect(const std::string& address) = 0;
    
    /**
     * @brief Stop listening and close the transport
     */
    virtual void Close() = 0;
    
    /**
     * @brief Check if the transport is actively listening
     * @return true if listening, false otherwise
     */
    virtual bool IsListening() const = 0;
    
    /**
     * @brief Get the file descriptor for the listening socket (for polling)
     * @return File descriptor, or -1 if not listening
     */
    virtual int GetListenFileDescriptor() const = 0;
    
    /**
     * @brief Set callback for when a new connection is accepted
     * @param callback Function to call when a connection is accepted
     */
    virtual void SetAcceptCallback(AcceptCallback callback) = 0;
};

} // namespace Perun::Transport
