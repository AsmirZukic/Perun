#!/usr/bin/env python3
import time
import random
import struct
import sys
from perun_sdk import PerunConnection, VideoFramePacket, InputEventPacket, PacketType

class Chip8:
    def __init__(self):
        self.memory = bytearray(4096)
        self.V = bytearray(16)
        self.I = 0
        self.pc = 0x200
        self.gfx = bytearray(64 * 32) # 0 or 1
        self.delay_timer = 0
        self.sound_timer = 0
        self.stack = []
        self.sp = 0
        self.key = bytearray(16)
        self.draw_flag = False
        
        # Load fontset
        fontset = [
            0xF0, 0x90, 0x90, 0x90, 0xF0, # 0
            0x20, 0x60, 0x20, 0x20, 0x70, # 1
            0xF0, 0x10, 0xF0, 0x80, 0xF0, # 2
            0xF0, 0x10, 0xF0, 0x10, 0xF0, # 3
            0x90, 0x90, 0xF0, 0x10, 0x10, # 4
            0xF0, 0x80, 0xF0, 0x10, 0xF0, # 5
            0xF0, 0x80, 0xF0, 0x90, 0xF0, # 6
            0xF0, 0x10, 0x20, 0x40, 0x40, # 7
            0xF0, 0x90, 0xF0, 0x90, 0xF0, # 8
            0xF0, 0x90, 0xF0, 0x10, 0xF0, # 9
            0xF0, 0x90, 0xF0, 0x90, 0x90, # A
            0xE0, 0x90, 0xE0, 0x90, 0xE0, # B
            0xF0, 0x80, 0x80, 0x80, 0xF0, # C
            0xE0, 0x90, 0x90, 0x90, 0xE0, # D
            0xF0, 0x80, 0xF0, 0x80, 0xF0, # E
            0xF0, 0x80, 0xF0, 0x80, 0x80  # F
        ]
        self.memory[0:len(fontset)] = bytearray(fontset)
        
    def load_rom(self, filename):
        try:
            with open(filename, 'rb') as f:
                data = f.read()
                self.memory[0x200:0x200+len(data)] = data
            print(f"Loaded ROM: {len(data)} bytes")
        except FileNotFoundError:
            print("ROM file not found")
            sys.exit(1)
            
    def load_test_program(self):
        # Writes "IBM" logo (or similar test)
        # 00E0 - CLS
        # 6000 - LD V0, 0
        # 6100 - LD V1, 0
        # A220 - LD I, 220 (Sprite address, wait, font is at 0)
        # D015 - DRW V0, V1, 5
        # ...
        # Let's just draw font characters 0-F at random positions
        
        program = []
        
        # Loop:
        # 1. Random X (V0)
        # 2. Random Y (V1)
        # 3. Random Char (V2)
        # 4. Set I to font char address
        # 5. Draw
        # 6. Jump to start
        
        # We need pseudo-random in Chip-8 opcode CXNN (RND VX, NN)
        
        ops = [
            0x00E0, # CLS
            # Start loop
            0xC03F, # RND V0, 0x3F (63) -> X
            0xC11F, # RND V1, 0x1F (31) -> Y
            0xC20F, # RND V2, 0x0F (15) -> Char
            
            # Set I = V2 * 5
            # Since we can't do multiply easily without loop, let's just pick Font 0
            # F229 - LD F, V2 (Set I = location of sprite for digit Vx)
            0xF229,
            
            # Draw
            0xD015, # DRW V0, V1, 5
            
            0x1202  # JP 0x202 (Loop)
        ]
        
        offset = 0x200
        for opcode in ops:
            self.memory[offset] = (opcode >> 8) & 0xFF
            self.memory[offset+1] = opcode & 0xFF
            offset += 2
            
    def cycle(self):
        # Fetch
        opcode = (self.memory[self.pc] << 8) | self.memory[self.pc + 1]
        
        # Decode & Execute
        self.pc += 2
        
        x = (opcode & 0x0F00) >> 8
        y = (opcode & 0x00F0) >> 4
        
        # print(f"PC: {self.pc-2:04X} Op: {opcode:04X}")
        
        if opcode == 0x00E0: # CLS
            self.gfx = bytearray(64 * 32)
            self.draw_flag = True
        elif opcode == 0x00EE: # RET
            self.pc = self.stack.pop()
        elif (opcode & 0xF000) == 0x1000: # JP addr
            self.pc = opcode & 0x0FFF
        elif (opcode & 0xF000) == 0x2000: # CALL addr
            self.stack.append(self.pc)
            self.pc = opcode & 0x0FFF
        elif (opcode & 0xF000) == 0x3000: # SE Vx, byte
            if self.V[x] == (opcode & 0x00FF):
                self.pc += 2
        elif (opcode & 0xF000) == 0x4000: # SNE Vx, byte
            if self.V[x] != (opcode & 0x00FF):
                self.pc += 2
        elif (opcode & 0xF000) == 0x5000: # SE Vx, Vy
            if self.V[x] == self.V[y]:
                self.pc += 2
        elif (opcode & 0xF000) == 0x6000: # LD Vx, byte
            self.V[x] = opcode & 0x00FF
        elif (opcode & 0xF000) == 0x7000: # ADD Vx, byte
            self.V[x] = (self.V[x] + (opcode & 0x00FF)) & 0xFF
        elif (opcode & 0xF000) == 0x8000:
            if (opcode & 0x000F) == 0: # LD Vx, Vy
                self.V[x] = self.V[y]
            elif (opcode & 0x000F) == 1: # OR Vx, Vy
                self.V[x] |= self.V[y]
            elif (opcode & 0x000F) == 2: # AND Vx, Vy
                self.V[x] &= self.V[y]
            elif (opcode & 0x000F) == 3: # XOR Vx, Vy
                self.V[x] ^= self.V[y]
            elif (opcode & 0x000F) == 4: # ADD Vx, Vy
                val = self.V[x] + self.V[y]
                self.V[0xF] = 1 if val > 255 else 0
                self.V[x] = val & 0xFF
            elif (opcode & 0x000F) == 5: # SUB Vx, Vy
                self.V[0xF] = 1 if self.V[x] > self.V[y] else 0
                self.V[x] = (self.V[x] - self.V[y]) & 0xFF
            elif (opcode & 0x000F) == 6: # SHR Vx
                self.V[0xF] = self.V[x] & 0x1
                self.V[x] >>= 1
            elif (opcode & 0x000F) == 7: # SUBN Vx, Vy
                self.V[0xF] = 1 if self.V[y] > self.V[x] else 0
                self.V[x] = (self.V[y] - self.V[x]) & 0xFF
            elif (opcode & 0x000F) == 0xE: # SHL Vx
                self.V[0xF] = (self.V[x] >> 7) & 0x1
                self.V[x] = (self.V[x] << 1) & 0xFF
        elif (opcode & 0xF000) == 0x9000: # SNE Vx, Vy
            if self.V[x] != self.V[y]:
                self.pc += 2
        elif (opcode & 0xF000) == 0xA000: # LD I, addr
            self.I = opcode & 0x0FFF
        elif (opcode & 0xF000) == 0xB000: # JP V0, addr
            self.pc = (opcode & 0x0FFF) + self.V[0]
        elif (opcode & 0xF000) == 0xC000: # RND Vx, byte
            self.V[x] = random.randint(0, 255) & (opcode & 0x00FF)
        elif (opcode & 0xF000) == 0xD000: # DRW Vx, Vy, nibble
            height = opcode & 0x000F
            pixel = 0
            self.V[0xF] = 0
            for yline in range(height):
                pixel = self.memory[self.I + yline]
                for xline in range(8):
                    if (pixel & (0x80 >> xline)) != 0:
                        idx = (self.V[x] + xline + ((self.V[y] + yline) * 64)) % len(self.gfx)
                        if self.gfx[idx] == 1:
                            self.V[0xF] = 1
                        self.gfx[idx] ^= 1
            self.draw_flag = True
        elif (opcode & 0xF000) == 0xE000:
            if (opcode & 0x00FF) == 0x9E: # SKP Vx
                if self.key[self.V[x]] != 0:
                    self.pc += 2
            elif (opcode & 0x00FF) == 0xA1: # SKNP Vx
                if self.key[self.V[x]] == 0:
                    self.pc += 2
        elif (opcode & 0xF000) == 0xF000:
            if (opcode & 0x00FF) == 0x07: # LD Vx, DT
                self.V[x] = self.delay_timer
            elif (opcode & 0x00FF) == 0x0A: # LD Vx, K
                pressed = False
                for i in range(16):
                    if self.key[i] != 0:
                        self.V[x] = i
                        pressed = True
                if not pressed:
                    self.pc -= 2
            elif (opcode & 0x00FF) == 0x15: # LD DT, Vx
                self.delay_timer = self.V[x]
            elif (opcode & 0x00FF) == 0x18: # LD ST, Vx
                self.sound_timer = self.V[x]
            elif (opcode & 0x00FF) == 0x1E: # ADD I, Vx
                self.I += self.V[x]
            elif (opcode & 0x00FF) == 0x29: # LD F, Vx
                self.I = self.V[x] * 5
            elif (opcode & 0x00FF) == 0x33: # LD B, Vx
                self.memory[self.I] = self.V[x] // 100
                self.memory[self.I+1] = (self.V[x] // 10) % 10
                self.memory[self.I+2] = (self.V[x] % 10)
            elif (opcode & 0x00FF) == 0x55: # LD [I], Vx
                for i in range(x + 1):
                    self.memory[self.I + i] = self.V[i]
            elif (opcode & 0x00FF) == 0x65: # LD Vx, [I]
                for i in range(x + 1):
                    self.V[i] = self.memory[self.I + i]

        if self.delay_timer > 0:
            self.delay_timer -= 1
        if self.sound_timer > 0:
            self.sound_timer -= 1
            if self.sound_timer == 1:
                print("BEEP!")

def main():
    if len(sys.argv) < 1:
        print("Usage: chip8.py [rom_file]")
        return
        
    chip8 = Chip8()
    if len(sys.argv) > 1:
        chip8.load_rom(sys.argv[1])
    else:
        print("No ROM provided, loading internal test program")
        chip8.load_test_program()
        
    # Connect to Perun Server
    conn = PerunConnection()
    if not conn.connect_tcp("127.0.0.1", 8080):
        if not conn.connect_unix("/tmp/perun.sock"):
            print("Failed to connect to Perun server")
            return
            
    print("Connected to Perun Server")
    
    last_time = time.time()
    
    try:
        while True:
            # Emulate ONE cycle
            # Chip-8 runs at ~500Hz
            # But we want to send frames at 60Hz
            
            # Simple loop: Run 8 instructions, then sleep/draw
            for _ in range(8):
                chip8.cycle()
            
            if chip8.draw_flag:
                chip8.draw_flag = False
                
                # Convert gfx to RGBA
                # 64x32
                rgba = bytearray(64 * 32 * 4)
                for i in range(64 * 32):
                    if chip8.gfx[i]:
                        # White
                        rgba[i*4] = 255
                        rgba[i*4+1] = 255
                        rgba[i*4+2] = 255
                        rgba[i*4+3] = 255
                    else:
                        # Black
                        rgba[i*4] = 0
                        rgba[i*4+1] = 0
                        rgba[i*4+2] = 0
                        rgba[i*4+3] = 255
                        
                packet = VideoFramePacket(
                    width=64,
                    height=32,
                    compressed_data=rgba
                )
                conn.send_video_frame(packet)
                
            # Handle input
            while True:
                result = conn.receive_packet_header()
                if not result:
                    break
                header, payload = result
                if header.type == PacketType.InputEvent:
                    pkt = InputEventPacket.deserialize(payload)
                    # Map buttons to chip8 keys
                    # Simple mapping: 0-F -> 0-F
                    # InputEventPacket buttons is bitmask.
                    # We need to update chip8.key array
                    for i in range(16):
                        if (pkt.buttons & (1 << i)) != 0:
                            chip8.key[i] = 1
                        else:
                            chip8.key[i] = 0
                            
            # Sync to ~60Hz
            current_time = time.time()
            elapsed = current_time - last_time
            if elapsed < 1/60.0:
                time.sleep((1/60.0) - elapsed)
            last_time = time.time()
            
    except KeyboardInterrupt:
        print("Exiting...")
    finally:
        conn.close()

if __name__ == "__main__":
    main()
