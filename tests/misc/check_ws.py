
import socket
import struct
import base64
import os

def check_ws():
    HOST = '127.0.0.1'
    PORT = 8081
    
    # 1. Connect
    print(f"Connecting to {HOST}:{PORT}...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    
    # 2. Handshake
    key = base64.b64encode(os.urandom(16)).decode('utf-8')
    request = (
        f"GET / HTTP/1.1\r\n"
        f"Host: {HOST}:{PORT}\r\n"
        f"Upgrade: websocket\r\n"
        f"Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        f"Sec-WebSocket-Version: 13\r\n"
        f"\r\n"
    )
    s.sendall(request.encode('utf-8'))
    
    # 3. Read handshake response
    response_data = s.recv(4096)
    print("--- Handshake Response ---")
    print(response_data.decode('utf-8', errors='replace'))
    
    if b"101 Switching Protocols" not in response_data:
        print("FAIL: Did not get 101 response")
        return

    # 4. Send PERUN_HELLO
    # "PERUN_HELLO" (11) + Version(2) + Caps(2) = 15 bytes
    magic = b"PERUN_HELLO"
    version = 1
    caps = 0
    payload = magic + struct.pack("!HH", version, caps)
    
    # Frame it (fin=1, opcode=2(binary), mask=1)
    # Payload len 15 (0x0F)
    # Byte 0: 1000 0010 = 0x82
    # Byte 1: 1000 1111 = 0x8F (Mask bit set + len 15)
    # Masking key (4 bytes)
    mask = b"\x00\x00\x00\x00" # Null mask for simplicity
    
    header = bytes([0x82, 0x8F]) + mask
    # Mask payload (xor with 0) -> same payload
    frame = header + payload
    
    print(f"Sending Frame ({len(frame)} bytes)...")
    s.sendall(frame)
    
    # 5. Read OK Response
    # Expect: 0x82, Len=6, Payload="OK"+Vers+Caps
    data = s.recv(4096)
    print(f"Received {len(data)} bytes")
    print("RAW:", data.hex())
    
    if len(data) == 0:
        print("FAIL: Server closed connection")
        return
        
    if data[0] != 0x82:
        print(f"FAIL: Invalid opcode/fin: {data[0]:02X}")
        return
        
    plen = data[1] & 0x7F
    print(f"Payload Len: {plen}")
    
    payload = data[2:2+plen]
    print("Payload:", payload)
    print("Payload Hex:", payload.hex())
    
    if payload.startswith(b"OK"):
        print("SUCCESS: Received OK packet")
    else:
        print("FAIL: Payload does not start with OK")

if __name__ == "__main__":
    check_ws()
