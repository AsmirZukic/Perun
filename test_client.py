import mmap
import os
import time
import struct

SHM_NAME = "/perun_shm"
SHM_SIZE = 4 * 1024 * 1024 # 4MB
WIDTH = 640
HEIGHT = 480

print(f"Opening Shared Memory: {SHM_NAME}")
try:
    # In Linux, shm_open usually creates file in /dev/shm
    # Python's mmap needs a file descriptor.
    # We can try to open /dev/shm/perun_shm if perun-server created it.
    
    # Wait for server to start
    shm_path = f"/dev/shm{SHM_NAME}"
    while not os.path.exists(shm_path):
        print("Waiting for perun-server to create SHM...")
        time.sleep(1)

    fd = os.open(shm_path, os.O_RDWR)
    shm = mmap.mmap(fd, SHM_SIZE)
    
    import socket

    SOCKET_PATH = "/perun.sock" # /tmp gets stuck sometimes, check local dir? No sticking to /tmp
    SOCKET_PATH = "/tmp/perun.sock"

    # Wait for socket
    while not os.path.exists(SOCKET_PATH):
        print("Waiting for perun.sock...")
        time.sleep(1)

    print("Connecting to Socket...")
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.connect(SOCKET_PATH)

    print("SHM Opened! Writing frames...")
    
    # Animate some colors
    for i in range(100):
        # Create a solid color buffer
        color = struct.pack('B', i) * 4 
        
        # Fill first line
        shm.seek(0)
        shm.write(color * 640)
        
        # Signal Update
        client.send(b'U')
        
        time.sleep(0.016)

    print("Done.")
    client.close()
    shm.close()
    os.close(fd)

except Exception as e:
    print(f"Error: {e}")
