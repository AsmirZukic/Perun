import sys
import os
import struct
import time

# Add local bindings to path for test
# Assuming running from project root: sys.path.append('bindings/python')
# Or if running script directly: resolve relative to script
script_dir = os.path.dirname(os.path.abspath(__file__))
project_root = os.path.abspath(os.path.join(script_dir, "../../"))
sys.path.append(os.path.join(project_root, 'bindings/python'))

try:
    from perun import PerunClient
except ImportError:
    print("Failed to import PerunClient. Make sure you run from project root or correct paths.")
    sys.exit(1)

def main():
    print("Testing Perun Python Bindings...")
    # Default behavior: look for socket in /tmp or relative?
    # PerunClient uses /tmp/perun.sock by default which is absolute.
    
    client = PerunClient(640, 480)
    
    print("Connecting...")
    try:
        client.connect()
    except Exception as e:
        print(f"Connection failed: {e}")
        print("Ensure 'perun-server' is running!")
        return

    print("Connected! Sending frames...")
    
    pixel_count = 640 * 480
    
    for i in range(120):
        # dynamic pattern
        intensity = (i * 4) % 255
        color = struct.pack('B', intensity) * 4 # Gray
        data = color * pixel_count
        
        client.update(data)
        time.sleep(0.016)
        
    print("Done.")
    client.close()

if __name__ == "__main__":
    main()
