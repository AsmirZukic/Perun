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
        view.setUint16(magic.length + 2, 0, false); // No caps
        
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
        } else if (type === PacketType.InputEvent) {
             // Echoed input event, ignore
        } else {
            this.log("Unknown packet type: " + type);
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
            
            // Log occasionally
            if (Math.random() < 0.02) {
                this.log(`Frame: ${width}x${height}, bytes=${pixelDataLength}`);
            }

            // Initialize/Resize Canvas to match source
            if (this.canvas.width !== width || this.canvas.height !== height) {
                this.log(`Resize canvas: ${width}x${height}`);
                this.canvas.width = width;
                this.canvas.height = height;
            }
            
            // Ensure imageData exists
            if (!this.imageData || this.imageData.width !== width || this.imageData.height !== height) {
                this.imageData = this.ctx.createImageData(width, height);
            }
            
            // Copy pixels directly
            const expectedSize = width * height * 4;
            if (pixelData.length === expectedSize) {
                this.imageData.data.set(pixelData);
                this.ctx.putImageData(this.imageData, 0, 0);
            } else {
                this.log(`Size mismatch: got ${pixelData.length}, need ${expectedSize}`);
            }
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
