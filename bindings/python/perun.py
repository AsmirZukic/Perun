import mmap
import os
import socket
import struct
import time

class PerunClient:
    def __init__(self, width=640, height=480):
        self.width = width
        self.height = height
        self.shm_name = "/perun_shm"
        self.socket_path = "/tmp/perun.sock"
        self.shm_size = width * height * 4
        
        self.shm = None
        self.sock = None
        self.fd = -1

    def connect(self, timeout=10.0):
        start = time.time()
        while not os.path.exists(self.socket_path):
            if time.time() - start > timeout:
                raise TimeoutError("Perun Server not found (Socket missing)")
            time.sleep(0.1)

        # Open Socket
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(self.socket_path)
        
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
            
        self.shm.seek(0)
        self.shm.write(pixels_bytes)
        
        # Signal Update (1 byte)
        self.sock.send(b'U')

    def close(self):
        if self.shm: self.shm.close()
        if self.fd != -1: os.close(self.fd)
        if self.sock: self.sock.close()
