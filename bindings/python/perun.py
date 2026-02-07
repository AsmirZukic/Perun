import mmap
import os
import socket
import struct
import time

class Keys:
    # Essential Scancodes
    A = 4
    B = 5
    C = 6
    D = 7
    E = 8
    F = 9
    G = 10
    H = 11
    I = 12
    J = 13
    K = 14
    L = 15
    M = 16
    N = 17
    O = 18
    P = 19
    Q = 20
    R = 21
    S = 22
    T = 23
    U = 24
    V = 25
    W = 26
    X = 27
    Y = 28
    Z = 29
    
    NUM_1 = 30
    NUM_2 = 31
    NUM_3 = 32
    NUM_4 = 33
    NUM_5 = 34
    NUM_6 = 35
    NUM_7 = 36
    NUM_8 = 37
    NUM_9 = 38
    NUM_0 = 39
    
    RETURN = 40
    ESCAPE = 41
    BACKSPACE = 42
    TAB = 43
    SPACE = 44

class PerunClient:
    def __init__(self, width=640, height=480):
        self.width = width
        self.height = height
        self.shm_name = os.environ.get("PERUN_SHM_NAME", "/perun_shm")
        self.socket_path = os.environ.get("PERUN_SOCKET_PATH", "/tmp/perun.sock")
        self.shm_size = width * height * 4
        
        self.shm = None
        self.sock = None
        self.fd = -1

    def connect(self, timeout=10.0):
        if not os.path.exists(self.socket_path):
             print("[PerunClient] Server not running. Starting...")
             try:
                 server_path = os.environ.get("PERUN_SERVER_PATH", "/home/asmir/Projects/Perun/build/perun-server")
                 if not os.path.exists(server_path):
                      raise FileNotFoundError(f"Could not find perun-server at {server_path}")
                 
                 import subprocess
                 self.server_process = subprocess.Popen([server_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
             except Exception as e:
                 raise RuntimeError(f"Failed to start Perun Server: {e}")

        start = time.time()
        while not os.path.exists(self.socket_path):
            if time.time() - start > timeout:
                raise TimeoutError("Perun Server not found (Socket missing after launch)")
            time.sleep(0.1)

        # Open Socket
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(self.socket_path)
        self.sock.setblocking(False) # Non-blocking for event polling
        
        # Open SHM (Server creates it)
        shm_path = f"/dev/shm{self.shm_name}"
        if not os.path.exists(shm_path):
             raise FileNotFoundError(f"Shared Memory not found at {shm_path}")
             
        self.fd = os.open(shm_path, os.O_RDWR)
        self.shm = mmap.mmap(self.fd, self.shm_size)
    
    def update(self, pixels_bytes):
        """
        Uploads pixels to SHM and signals update.
        pixels_bytes: bytes object of RGBA data.
        """
        if len(pixels_bytes) != self.shm_size:
            raise ValueError(f"Expected {self.shm_size} bytes, got {len(pixels_bytes)}")
            
        if not self.sock:
             return

        self.shm.seek(0)
        self.shm.write(pixels_bytes)
        
        # Signal Update (1 byte)
        try:
            self.sock.send(b'U')
        except BlockingIOError:
            pass # buffer full, skip signal
        except (BrokenPipeError, ConnectionResetError):
            self.close() # Connection dead

    def send_sound(self, enable: bool):
        """
        Sends sound control packet.
        enable: True to start playing, False to stop.
        """
        if not self.sock: return
        try:
            payload = b'S' + (b'\x01' if enable else b'\x00')
            self.sock.send(payload)
        except (BlockingIOError, BrokenPipeError, ConnectionResetError):
            pass

    def poll_events(self):
        """
        Reads pending events from the socket.
        Returns a list of event objects/dicts.
        Example: {'type': 'K', 'pressed': True, 'key': 4}
        """
        events = []
        if not self.sock:
             return events

        try:
            while True:
                # Header: Type(1) + Pressed(1) + Scancode(2) = 4 bytes
                data = self.sock.recv(4)
                if not data:
                    self.close() # EOF means disconnected
                    break
                
                if len(data) < 4:
                     # Partial read? In non-blocking UNIX socket this is rare for small packets
                     # but strictly we should buffer. For this simple case, we assume atomicity or drop.
                     break

                type_char = chr(data[0])
                if type_char == 'K':
                    pressed = (data[1] == 1)
                    scancode = struct.unpack('>H', data[2:4])[0] # Network byte order
                    events.append({'type': type_char, 'pressed': pressed, 'key': scancode})
                    
        except BlockingIOError:
            pass # No more data
        except (ConnectionResetError, BrokenPipeError):
             self.close() # Connection dead
            
        return events

    def close(self):
        if self.shm: 
             self.shm.close()
             self.shm = None
        if self.fd != -1: 
             os.close(self.fd)
             self.fd = -1
        if self.sock: 
             self.sock.close()
             self.sock = None
