#include <gtest/gtest.h>
#include "Perun/Transport/UnixTransport.h"
#include <thread>
#include <chrono>
#include <cstring>

using namespace Perun::Transport;

class UnixTransportTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_socketPath = "/tmp/perun_test.sock";
        // Clean up any existing socket
        unlink(m_socketPath.c_str());
    }
    
    void TearDown() override {
        // Clean up socket file
        unlink(m_socketPath.c_str());
    }
    
    std::string m_socketPath;
};

TEST_F(UnixTransportTest, ListenAndClose) {
    UnixTransport transport;
    
    EXPECT_FALSE(transport.IsListening());
    EXPECT_TRUE(transport.Listen(m_socketPath));
    EXPECT_TRUE(transport.IsListening());
    EXPECT_GE(transport.GetListenFileDescriptor(), 0);
    
    transport.Close();
    EXPECT_FALSE(transport.IsListening());
}

TEST_F(UnixTransportTest, ConnectToServer) {
    UnixTransport server;
    ASSERT_TRUE(server.Listen(m_socketPath));
    
    // Connect from client
    UnixTransport client;
    auto clientConn = client.Connect(m_socketPath);
    
    ASSERT_NE(clientConn, nullptr);
    EXPECT_TRUE(clientConn->IsOpen());
    EXPECT_GE(clientConn->GetFileDescriptor(), 0);
    
    // Accept on server side
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto serverConn = server.Accept();
    
    ASSERT_NE(serverConn, nullptr);
    EXPECT_TRUE(serverConn->IsOpen());
    
    clientConn->Close();
    serverConn->Close();
}

TEST_F(UnixTransportTest, SendAndReceive) {
    UnixTransport server;
    ASSERT_TRUE(server.Listen(m_socketPath));
    
    UnixTransport client;
    auto clientConn = client.Connect(m_socketPath);
    ASSERT_NE(clientConn, nullptr);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto serverConn = server.Accept();
    ASSERT_NE(serverConn, nullptr);
    
    // Send from client to server
    const char* message = "Hello, Server!";
    ssize_t sent = clientConn->Send(reinterpret_cast<const uint8_t*>(message), strlen(message));
    EXPECT_EQ(sent, strlen(message));
    
    // Receive on server
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    uint8_t buffer[256];
    ssize_t received = serverConn->Receive(buffer, sizeof(buffer));
    ASSERT_GT(received, 0);
    buffer[received] = '\0';
    EXPECT_STREQ(reinterpret_cast<char*>(buffer), message);
    
    // Send from server to client
    const char* response = "Hello, Client!";
    sent = serverConn->Send(reinterpret_cast<const uint8_t*>(response), strlen(response));
    EXPECT_EQ(sent, strlen(response));
    
    // Receive on client
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    received = clientConn->Receive(buffer, sizeof(buffer));
    ASSERT_GT(received, 0);
    buffer[received] = '\0';
    EXPECT_STREQ(reinterpret_cast<char*>(buffer), response);
}

TEST_F(UnixTransportTest, ConnectionClosed) {
    UnixTransport server;
    ASSERT_TRUE(server.Listen(m_socketPath));
    
    UnixTransport client;
    auto clientConn = client.Connect(m_socketPath);
    ASSERT_NE(clientConn, nullptr);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto serverConn = server.Accept();
    ASSERT_NE(serverConn, nullptr);
    
    // Close client connection
    clientConn->Close();
    EXPECT_FALSE(clientConn->IsOpen());
    
    // Server should detect closed connection on next receive
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    uint8_t buffer[256];
    ssize_t received = serverConn->Receive(buffer, sizeof(buffer));
    EXPECT_EQ(received, 0);  // Connection closed
    EXPECT_FALSE(serverConn->IsOpen());
}

TEST_F(UnixTransportTest, MultipleConnections) {
    UnixTransport server;
    ASSERT_TRUE(server.Listen(m_socketPath));
    
    // Create multiple client connections
    UnixTransport client1, client2;
    auto conn1 = client1.Connect(m_socketPath);
    auto conn2 = client2.Connect(m_socketPath);
    
    ASSERT_NE(conn1, nullptr);
    ASSERT_NE(conn2, nullptr);
    
    // Accept both connections
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto serverConn1 = server.Accept();
    auto serverConn2 = server.Accept();
    
    ASSERT_NE(serverConn1, nullptr);
    ASSERT_NE(serverConn2, nullptr);
    
    // Verify they are different connections
    EXPECT_NE(serverConn1->GetFileDescriptor(), serverConn2->GetFileDescriptor());
}

TEST_F(UnixTransportTest, AcceptCallback) {
    UnixTransport server;
    
    bool callbackCalled = false;
    std::shared_ptr<IConnection> acceptedConn;
    
    server.SetAcceptCallback([&](std::shared_ptr<IConnection> conn) {
        callbackCalled = true;
        acceptedConn = conn;
    });
    
    ASSERT_TRUE(server.Listen(m_socketPath));
    
    UnixTransport client;
    auto clientConn = client.Connect(m_socketPath);
    ASSERT_NE(clientConn, nullptr);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto serverConn = server.Accept();
    
    EXPECT_TRUE(callbackCalled);
    EXPECT_EQ(serverConn, acceptedConn);
}
