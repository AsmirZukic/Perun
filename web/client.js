// Perun Web Client

class PerunClient {
    constructor(canvasId, statusId) {
        this.canvas = document.getElementById(canvasId);
        this.ctx = this.canvas.getContext('2d');
        this.statusEl = document.getElementById(statusId);
        this.socket = null;
        this.connected = false;
        
        // Input state
        this.keys = 0;
        
        // Protocol
        this.handshakeComplete = false;
        
        // Frame buffer
        this.imageData = null;
    }
    
    connect(url) {
        this.updateStatus("Connecting...");
        this.socket = new WebSocket(url);
        this.socket.binaryType = "arraybuffer";
        
        this.socket.onopen = () => {
            this.updateStatus("Connected! Handshaking...");
            this.sendHello();
        };
        
        this.socket.onmessage = (event) => {
            this.handleMessage(event.data);
        };
        
        this.socket.onclose = () => {
            this.updateStatus("Disconnected");
            this.connected = false;
            this.handshakeComplete = false;
        };
        
        this.socket.onerror = (error) => {
            console.error("WebSocket error:", error);
            this.updateStatus("Error");
        };
        
        // Input listeners
        window.addEventListener('keydown', (e) => this.handleKey(e, true));
        window.addEventListener('keyup', (e) => this.handleKey(e, false));
    }
    
    updateStatus(msg) {
        this.statusEl.textContent = msg;
    }
    
    sendHello() {
        // MAGIC "PERUN_HELLO" + Version(2) + Caps(2)
        const magic = new TextEncoder().encode("PERUN_HELLO");
        const buffer = new ArrayBuffer(magic.length + 4);
        const view = new DataView(buffer);
        
        new Uint8Array(buffer).set(magic, 0);
        view.setUint16(magic.length, 1, false); // Version 1 (Big Endian)
        // Request CAP_DELTA (0x01) + CAP_AUDIO (0x02) = 0x03
        view.setUint16(magic.length + 2, Capabilities.CAP_DELTA | Capabilities.CAP_AUDIO, false);
        
        this.socket.send(buffer);
    }

    
    handleMessage(data) {
        if (!this.handshakeComplete) {
            // Expecting OK response
            this.log("Received handshake data, size: " + data.byteLength);
            
            if (data.byteLength >= 2) {
                const view = new DataView(data);
                if (view.getUint8(0) === 0x4F && view.getUint8(1) === 0x4B) { // 'O', 'K'
                    this.handshakeComplete = true;
                    this.connected = true;
                    this.updateStatus("Ready (Handshake OK)");
                    this.log("Handshake complete");
                    return;
                }
            }
            
            this.log("Handshake failed, invalid response");
            this.updateStatus("Handshake Failed");
            this.socket.close();
            return;
        }
        
        // Handle Packet
        // Header: Type(1) Flags(1) Sequence(2) Length(4)
        if (data.byteLength < 8) {
            this.log("Packet too small: " + data.byteLength);
            return;
        }
        
        const view = new DataView(data);
        const type = view.getUint8(0);
        const flags = view.getUint8(1);
        const sequence = view.getUint16(2, false);
        const length = view.getUint32(4, false); // Big Endian
        
        // Only log 2% of packets to avoid spamming the UI
        if (Math.random() < 0.02) {
             this.log(`Packet: type=${type}, seq=${sequence}, len=${length}, total=${data.byteLength}`);
        }
        
        if (data.byteLength < 8 + length) {
            this.log(`Incomplete packet, need ${8+length}, got ${data.byteLength}`);
            return;
        }
        
        const payload = new Uint8Array(data, 8, length);
        
        if (type === PacketType.VideoFrame) {
            this.handleVideoFrame(payload, flags);
        } else if (type === PacketType.AudioChunk) {
            this.handleAudioChunk(payload);
        } else if (type === PacketType.InputEvent) {
             // Echoed input event, ignore
        } else {
            this.log("Unknown packet type: " + type);
        }
    }
    
    handleAudioChunk(payload) {
        // AudioChunkPacket: sampleRate(2), channels(1), samples...
        // For Chip-8, samples[0] > 0 means beep on, 0 means off
        this.log(`AudioChunk received: ${payload.length} bytes`);
        
        if (payload.length < 5) {
            this.log(`AudioChunk too small: ${payload.length}`);
            return;
        }
        
        const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
        // Skip sampleRate(2) + channels(1), read first sample (int16)
        const signal = view.getInt16(3, false);
        
        this.log(`Audio signal: ${signal}`);
        
        if (signal > 0) {
            this.startBeep();
        } else {
            this.stopBeep();
        }
    }

    
    startBeep() {
        if (this.oscillator) return; // Already beeping
        
        try {
            if (!this.audioCtx) {
                this.audioCtx = new (window.AudioContext || window.webkitAudioContext)();
            }
            
            // Create oscillator for 440Hz square wave (classic beep)
            this.oscillator = this.audioCtx.createOscillator();
            this.oscillator.type = 'square';
            this.oscillator.frequency.value = 440;
            
            // Add gain node to control volume (reduce harshness)
            this.gainNode = this.audioCtx.createGain();
            this.gainNode.gain.value = 0.1; // 10% volume
            
            this.oscillator.connect(this.gainNode);
            this.gainNode.connect(this.audioCtx.destination);
            this.oscillator.start();
            
            this.log("Beep ON");
        } catch (e) {
            console.error("Audio error:", e);
        }
    }
    
    stopBeep() {
        if (this.oscillator) {
            try {
                this.oscillator.stop();
            } catch (e) { /* ignore */ }
            this.oscillator.disconnect();
            this.oscillator = null;
            this.log("Beep OFF");
        }
        if (this.gainNode) {
            this.gainNode.disconnect();
            this.gainNode = null;
        }
    }

    
    log(msg) {
        console.log(msg);
        const logEl = document.getElementById('debugLog');
        if (logEl) {
            const line = document.createElement('div');
            line.textContent = msg;
            logEl.appendChild(line);
            logEl.scrollTop = logEl.scrollHeight;
            // Limit log size
            while (logEl.children.length > 20) logEl.removeChild(logEl.firstChild);
        }
    }

    handleVideoFrame(payload, flags) {
        // Store latest frame for rendering
        this.pendingFrame = { payload, flags };
        
        if (!this.renderLoopStarted) {
            this.renderLoopStarted = true;
            requestAnimationFrame(() => this.renderLoop());
        }
    }
    
    renderLoop() {
        if (this.pendingFrame) {
            const { payload, flags } = this.pendingFrame;
            this.pendingFrame = null;
            this.renderFrame(payload, flags);
        }
        requestAnimationFrame(() => this.renderLoop());
    }

    renderFrame(payload, flags) {
        try {
            const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
            const width = view.getUint16(0, false);
            const height = view.getUint16(2, false);
            
            // Pixel data starts at offset 4
            const pixelDataLength = payload.byteLength - 4;
            const pixelData = new Uint8Array(payload.buffer, payload.byteOffset + 4, pixelDataLength);
            
            // Check flags for Delta Compression (0x02)
            const isDelta = (flags & 0x02) !== 0;

            // Log occasionally
            if (Math.random() < 0.02) {
                this.log(`Frame: ${width}x${height}, bytes=${pixelDataLength}, delta=${isDelta}`);
            }

            // Initialize/Resize Canvas to match source
            // Initialize/Resize Canvas to match source
            if (this.canvas.width !== width || this.canvas.height !== height) {
                this.log(`Resize canvas: ${width}x${height}`);
                this.canvas.width = width;
                this.canvas.height = height;
                this.imageData = null; // Force recreation
                
                // DEBUG: Fill green
                this.ctx.fillStyle = '#00FF00';
                this.ctx.fillRect(0,0,width,height);
            }
            
            // Ensure imageData exists
            if (!this.imageData || this.imageData.width !== width || this.imageData.height !== height) {
                this.imageData = this.ctx.createImageData(width, height);
                // Clear buffer (alpha 255)
                for (let i = 3; i < this.imageData.data.length; i += 4) {
                    this.imageData.data[i] = 255;
                }
            }

            const target = this.imageData.data;
            const expectedSize = width * height * 4;

            // Verify payload content
            let activeBytes = 0;
            for(let k=0; k<pixelData.length; k++) {
                if (pixelData[k] !== 0) activeBytes++;
            }
            if (activeBytes > 0) {
                 // console.log(`Frame received: bytes=${pixelData.length}, active=${activeBytes}, isDelta=${isDelta}`);
            } else {
                 console.warn(`Frame received: bytes=${pixelData.length}, BUT ALL ZEROS! isDelta=${isDelta}`);
            }

            if (isDelta) {
                // Apply XOR Delta
                // pixelData contains the XOR diff
                if (pixelData.length !== expectedSize) {
                     this.log(`Delta size mismatch: got ${pixelData.length}, need ${expectedSize}`);
                     return;
                }
                
                for (let i = 0; i < expectedSize; i++) {
                    target[i] ^= pixelData[i];
                }
                // Force alpha to 255 (opaque) - XOR on alpha can result in 0
                for (let i = 3; i < expectedSize; i += 4) {
                    target[i] = 255;
                }
            } else {
                // Full Frame
                // Copy pixels directly
                if (pixelData.length === expectedSize) {
                    target.set(pixelData);
                } else {
                    this.log(`Size mismatch: got ${pixelData.length}, need ${expectedSize}`);
                    return;
                }
            }
            
            // Put modified buffer to canvas
            this.ctx.putImageData(this.imageData, 0, 0);

        } catch (e) {
            console.error("Error in renderFrame:", e);
            this.updateStatus("Render Error: " + e.message);
            this.log("Error: " + e.message);
        }
    }
    
    handleKey(e, pressed) {
        if (!this.connected) return;
        
        // Map keys 1-4, Q-R, A-F, Z-V to Chip8 keypad
        const keyMap = {
            '1': 0x1, '2': 0x2, '3': 0x3, '4': 0xC,
            'q': 0x4, 'w': 0x5, 'e': 0x6, 'r': 0xD,
            'a': 0x7, 's': 0x8, 'd': 0x9, 'f': 0xE,
            'z': 0xA, 'x': 0x0, 'c': 0xB, 'v': 0xF
        };
        
        const key = e.key.toLowerCase();
        if (keyMap[key] !== undefined) {
            const bit = keyMap[key];
            if (pressed) {
                this.keys |= (1 << bit);
            } else {
                this.keys &= ~(1 << bit);
            }
            this.sendInput();
        }
    }
    
    sendInput() {
        // Send InputEventPacket
        // Header + Payload(Buttons(2), Reserved(2))
        const buffer = new ArrayBuffer(8 + 4);
        const view = new DataView(buffer);
        
        // Header
        view.setUint8(0, PacketType.InputEvent);
        view.setUint8(1, 0);
        view.setUint16(2, 0);
        view.setUint32(4, 4);
        
        // Payload
        view.setUint16(8, this.keys, false);
        view.setUint16(10, 0, false);
        
        this.socket.send(buffer);
    }
}

const client = new PerunClient('display', 'status');

document.getElementById('connectBtn').addEventListener('click', () => {
    client.connect('ws://localhost:8081');
});
