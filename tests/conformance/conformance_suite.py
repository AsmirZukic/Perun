import unittest
import time
import subprocess
import os
import signal
import sys
from perun_sdk import PerunConnection, PacketType, VideoFramePacket
# Adjust path to import MockEmulatorCore
sys.path.append(os.path.join(os.path.dirname(__file__), '..', '..'))
from tests.tools.mock_core import MockEmulatorCore

class TestConformance(unittest.TestCase):
    server_process = None
    socket_path = "/tmp/perun_test.sock"
    tcp_port = 8081
    tcp_host = "127.0.0.1"

    @classmethod
    def setUpClass(cls):
        # Start server in headless mode
        server_path = os.path.abspath("./build/perun-server")
        if not os.path.exists(server_path):
            raise RuntimeError("perun-server binary not found. Build it first.")
            
        # Clean up previous socket
        if os.path.exists(cls.socket_path):
            os.remove(cls.socket_path)
            
        cmd = [server_path, "--headless", "--unix", cls.socket_path, "--tcp", f":{cls.tcp_port}"]
        
        cls.server_process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        
        # Wait for server to start
        time.sleep(1)
        
        if cls.server_process.poll() is not None:
            valid_out, valid_err = cls.server_process.communicate()
            raise RuntimeError(f"Server failed to start:\n{valid_out.decode()}\n{valid_err.decode()}")

    @classmethod
    def tearDownClass(cls):
        if cls.server_process:
            cls.server_process.terminate()
            try:
                cls.server_process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                cls.server_process.kill()
                
        if os.path.exists(cls.socket_path):
            os.remove(cls.socket_path)

    def test_handshake_success(self):
        """Verify successful handshake and capability negotiation"""
        core = MockEmulatorCore(address=self.socket_path)
        self.assertTrue(core.connect(), "Failed to connect to Unix socket")
        self.assertTrue(core.connection._connected)
        self.assertNotEqual(core.connection._capabilities, 0)
        core.disconnect()

    def test_video_streaming(self):
        """Verify video streaming works without error"""
        core = MockEmulatorCore(address=self.socket_path)
        self.assertTrue(core.connect())
        
        # Send 10 frames
        for _ in range(10):
            core.generate_frame()
            self.assertTrue(core.send_frame())
            # Small delay to allow server validation (if any)
            time.sleep(0.01)
            
        core.disconnect()

    def test_tcp_connection(self):
        """Verify TCP connection works"""
        core = MockEmulatorCore(
            use_tcp=True, 
            tcp_port=self.tcp_port,
            tcp_host=self.tcp_host
        )
        self.assertTrue(core.connect(), "Failed to connect via TCP")
        self.assertTrue(core.connection._connected)
        
        # Send a frame over TCP
        core.generate_frame()
        self.assertTrue(core.send_frame())
        
        core.disconnect()

    def test_multiple_clients(self):
        """Verify multiple clients can connect simultaneously"""
        client1 = MockEmulatorCore(address=self.socket_path)
        client2 = MockEmulatorCore(
            use_tcp=True, 
            tcp_port=self.tcp_port,
            tcp_host=self.tcp_host
        )
        
        self.assertTrue(client1.connect())
        self.assertTrue(client2.connect())
        
        # Interleaved sending
        client1.generate_frame("solid_red")
        self.assertTrue(client1.send_frame())
        
        client2.generate_frame("gradient")
        self.assertTrue(client2.send_frame())
        
        client1.disconnect()
        client2.disconnect()

if __name__ == "__main__":
    unittest.main()
