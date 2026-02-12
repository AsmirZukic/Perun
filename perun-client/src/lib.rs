use std::cell::RefCell;
use std::rc::Rc;
use wasm_bindgen::prelude::*;
use wasm_bindgen::JsCast;
use web_sys::{
    BinaryType, CanvasRenderingContext2d, ErrorEvent, HtmlCanvasElement, MessageEvent, WebSocket,
    ImageData,
};
use wasm_bindgen::Clamped;
use perun_protocol::{PacketHeader, PacketType, VideoFramePacket, Handshake, capabilities};

macro_rules! console_log {
    ($($t:tt)*) => (log(&format_args!($($t)*).to_string()))
}

#[wasm_bindgen]
extern "C" {
    #[wasm_bindgen(js_namespace = console)]
    fn log(s: &str);
}

#[wasm_bindgen]
pub struct PerunClient {
    inner: Rc<RefCell<ClientInner>>,
}

struct ClientInner {
    canvas: HtmlCanvasElement,
    ctx: CanvasRenderingContext2d,
    ws: Option<WebSocket>,
    on_message_callback: Option<Closure<dyn FnMut(MessageEvent)>>,
    // We can store other callbacks here to keep them alive
    width: u32,
    height: u32,
    image_data_buffer: Vec<u8>,
    previous_frame_buffer: Vec<u8>,
}

#[wasm_bindgen]
impl PerunClient {
    #[wasm_bindgen(constructor)]
    pub fn new(canvas_id: &str) -> Result<PerunClient, JsValue> {
        console_error_panic_hook::set_once();
        
        let window = web_sys::window().expect("no global `window` exists");
        let document = window.document().expect("should have a document on window");
        let canvas = document
            .get_element_by_id(canvas_id)
            .ok_or("Canvas not found")?
            .dyn_into::<HtmlCanvasElement>()?;
            
        let ctx = canvas
            .get_context("2d")?
            .ok_or("2d context not supported")?
            .dyn_into::<CanvasRenderingContext2d>()?;

        Ok(PerunClient {
            inner: Rc::new(RefCell::new(ClientInner {
                canvas,
                ctx,
                ws: None,
                on_message_callback: None,
                width: 0,
                height: 0,
                image_data_buffer: Vec::new(),
                previous_frame_buffer: Vec::new(),
            })),
        })
    }

    pub fn connect(&mut self, url: &str) -> Result<(), JsValue> {
        console_log!("Connecting to {}", url);
        let ws = WebSocket::new(url)?;
        ws.set_binary_type(BinaryType::Arraybuffer);

        let inner_clone = self.inner.clone();
        
        // On Open
        let ws_clone = ws.clone();
        let onopen_callback = Closure::<dyn FnMut()>::new(move || {
            console_log!("Connected! Sending Handshake...");
            // Send Handshake HELLO
            let hello = Handshake::create_hello(1, capabilities::CAP_DELTA | capabilities::CAP_AUDIO);
            
            match ws_clone.send_with_u8_array(&hello) {
                Ok(_) => console_log!("Handshake sent"),
                Err(e) => console_log!("Error sending handshake: {:?}", e),
            }
        });
        ws.set_onopen(Some(onopen_callback.as_ref().unchecked_ref()));
        onopen_callback.forget(); // Leak for now, clean up later properly if needed

        // On Message
        let inner_msg = self.inner.clone();
        let onmessage_callback = Closure::<dyn FnMut(MessageEvent)>::new(move |e: MessageEvent| {
            if let Ok(abuf) = e.data().dyn_into::<js_sys::ArrayBuffer>() {
                let array = js_sys::Uint8Array::new(&abuf);
                let vec = array.to_vec();
                
                // Handle Packet
                
                // Simple handshake check: if message is "OK"+... generic check
                if vec.len() >= 2 && &vec[0..2] == b"OK" {
                    console_log!("Handshake OK");
                    return;
                }

                // Try to parse as Packet
                match PacketHeader::deserialize(&vec) {
                    Ok(header) => {
                         let payload = &vec[PacketHeader::SIZE..];
                         let mut inner = inner_msg.borrow_mut();
                         
                         // Debug: Print every packet type received
                         // inner.ctx.set_font("12px monospace");
                         // inner.ctx.set_fill_style(&"black".into());
                         // let _ = inner.ctx.fill_text(&format!("Rx Packet: {:?}", header.packet_type), 10.0, 20.0);

                         if header.packet_type == PacketType::VideoFrame {
                             // Note: deserialize now handles decompression internally!
                             // But wait, we need to pass the flags from the header to deserialize!
                             match VideoFramePacket::deserialize(payload, header.flags) {
                                Ok(frame) => {
                                    inner.render_frame(frame);
                                }
                                Err(e) => {
                                     console_log!("Failed to deserialize video frame: {:?}", e);
                                     inner.ctx.set_font("20px Arial");
                                     inner.ctx.set_fill_style(&"red".into());
                                     let _ = inner.ctx.fill_text(&format!("LZ4 Error: {:?}", e), 10.0, 40.0);
                                }
                             }
                         } else {
                             // Log other packet types
                             // console_log!("Received non-video packet: {:?}", header.packet_type);
                         }
                    }
                    Err(e) => {
                        console_log!("Header Parse Error: {:?}", e);
                        let mut inner = inner_msg.borrow_mut();
                        inner.ctx.set_font("20px Arial");
                        inner.ctx.set_fill_style(&"red".into());
                        let _ = inner.ctx.fill_text(&format!("Header Error: {:?} (len={})", e, vec.len()), 10.0, 40.0);
                    }
                }
            }
        });
        ws.set_onmessage(Some(onmessage_callback.as_ref().unchecked_ref()));
        // Store callback to keep it alive
        self.inner.borrow_mut().on_message_callback = Some(onmessage_callback);
        
        self.inner.borrow_mut().ws = Some(ws);
        
        Ok(())
    }
}

impl ClientInner {
    fn render_frame(&mut self, frame: VideoFramePacket) {
        if self.width != frame.width as u32 || self.height != frame.height as u32 {
             self.width = frame.width as u32;
             self.height = frame.height as u32;
             self.canvas.set_width(self.width);
             self.canvas.set_height(self.height);
             
             // Reallocate buffer (RGBA = 4 bytes)
             let size = (self.width * self.height * 4) as usize;
             self.image_data_buffer.resize(size, 255);
             self.previous_frame_buffer.resize(size, 0);
        }
        
        // frame.data is already decompressed by VideoFramePacket::deserialize
        // We just need to handle Delta logic
        
        if frame.is_delta {
            // Apply XOR Delta
            // WASM doesn't support u128 easily without SIMD flags, but we can use u64 chunks or simple loop
            // Simple loop is fast enough for WASM usually, but let's try u64 chunks if aligned
            
            let len = self.image_data_buffer.len().min(frame.data.len());
            
            // Fallback to simple loop for safety/simplicity in first pass
            for i in 0..len {
                self.image_data_buffer[i] = self.previous_frame_buffer[i] ^ frame.data[i];
            }
        } else {
            // Full Frame - just copy
            if frame.data.len() == self.image_data_buffer.len() {
                self.image_data_buffer.copy_from_slice(&frame.data);
            }
        }
        
        // Update previous frame for next delta
        self.previous_frame_buffer.copy_from_slice(&self.image_data_buffer);

        // Put image data
        if let Ok(image_data) = ImageData::new_with_u8_clamped_array_and_sh(
             wasm_bindgen::Clamped(&self.image_data_buffer[..]), 
             self.width, 
             self.height
        ) {
             let _ = self.ctx.put_image_data(&image_data, 0.0, 0.0);
        }

        // Draw overlay text for errors if needed (After put_image_data so it sits on top)
        if !frame.is_delta && frame.data.len() != (self.width * self.height * 4) as usize {
             self.ctx.set_font("20px Arial");
             self.ctx.set_fill_style(&"red".into());
             let msg = format!("Size: {} vs {}", frame.data.len(), self.image_data_buffer.len());
             let _ = self.ctx.fill_text(&msg, 10.0, 60.0);
        }
    }
}
