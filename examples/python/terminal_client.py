#!/usr/bin/env python3
import time
import sys
import os
import traceback
from perun_sdk import PerunConnection, VideoFramePacket, PacketType, PacketHeader, PacketFlags, apply_delta

def render_ascii(frame_data, width, height, term_w=80, term_h=24):
    """
    Render a frame to ASCII art.
    frame_data: RGBA bytes
    """
    chars = " .:-=+*#%@"
    scale_x = width / term_w
    scale_y = height / term_h
    output = []
    
    for ty in range(term_h):
        line = ""
        sy = int(ty * scale_y)
        for tx in range(term_w):
            sx = int(tx * scale_x)
            idx = (sy * width + sx) * 4
            if idx + 3 < len(frame_data):
                r = frame_data[idx]
                g = frame_data[idx+1]
                b = frame_data[idx+2]
                lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
                char_idx = int((lum / 255.0) * (len(chars) - 1))
                line += chars[char_idx]
            else:
                line += " "
        output.append(line)
    
    # ANSI clear screen
    sys.stdout.write("\033[2J\033[H")
    sys.stdout.write("\n".join(output))
    sys.stdout.write("\n")
    sys.stdout.flush()

def main():
    print("Starting Terminal Client...", flush=True)
    conn = PerunConnection()
    
    if not conn.connect_tcp("127.0.0.1", 8080):
        print("Failed to connect to server.", flush=True)
        return 1
        
    print("Connected! Waiting for frames...", flush=True)
    
    current_frame = None
    frame_count = 0
    
    try:
        while True:
            processed = 0
            while True:
                result = conn.receive_packet_header()
                if not result:
                    break
                
                processed += 1
                header, payload = result
                
                if header.type == PacketType.VideoFrame:
                    packet = VideoFramePacket.deserialize(payload)
                    is_delta = bool(header.flags & PacketFlags.DeltaFrame)
                    video_data = packet.compressed_data
                    
                    if is_delta:
                        if current_frame is None:
                            continue
                        try:
                            current_frame = apply_delta(video_data, current_frame)
                        except Exception as e:
                           current_frame = None 
                           continue
                    else:
                        current_frame = video_data
                        
                    frame_count += 1
                    
                    if frame_count % 5 == 0:
                        render_ascii(current_frame, packet.width, packet.height)
                        print(f"Frame: {frame_count} Size: {packet.width}x{packet.height} Delta: {is_delta}", flush=True)
            
            if processed == 0:
                time.sleep(0.001)
            
    except KeyboardInterrupt:
        print("\nExiting...", flush=True)
    except Exception:
        traceback.print_exc()
    
    conn.close()
    return 0

if __name__ == "__main__":
    main()
