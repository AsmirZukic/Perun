use perun_protocol::{VideoFramePacket, flags};
use std::time::{Instant, Duration};

pub struct FrameProcessor {
    last_frame: Vec<u8>,
    frame_count: u64,
    last_keyframe: Instant,
    force_keyframe_interval: Duration,
}

impl FrameProcessor {
    pub fn new() -> Self {
        Self {
            last_frame: Vec::new(),
            frame_count: 0,
            last_keyframe: Instant::now(),
            force_keyframe_interval: Duration::from_secs(1),
        }
    }

    pub fn process(&mut self, width: u16, height: u16, current_frame: &[u8]) -> (VideoFramePacket, u8) {
        let mut flags = 0u8;
        let is_delta;
        
        // Check if we should force a keyframe
        let force_keyframe = self.last_keyframe.elapsed() >= self.force_keyframe_interval;
        
        // 1. Compute Delta if possible
        let delta_data = if !force_keyframe && self.last_frame.len() == current_frame.len() {
            Some(self.compute_delta_simd(current_frame, &self.last_frame))
        } else {
            None
        };

        // 2. Compress both options
        // Option A: Compressed Full Frame
        let compressed_full = lz4_flex::compress_prepend_size(current_frame);
        
        // Option B: Compressed Delta (if available)
        let (best_data, used_delta) = if let Some(delta) = delta_data {
             let compressed_delta = lz4_flex::compress_prepend_size(&delta);
             
             // Heuristic: Use delta if it's significantly smaller (e.g. < 70% of full)
             // Or just strictly smaller? 
             // Let's say strictly smaller for now.
             if compressed_delta.len() < compressed_full.len() {
                 (compressed_delta, true)
             } else {
                 (compressed_full, false)
             }
        } else {
            (compressed_full, false)
        };
        
        // 3. Update state
        self.last_frame = current_frame.to_vec();
        if !used_delta {
            self.last_keyframe = Instant::now();
        }
        self.frame_count += 1;
        
        // 4. Set Flags & Construct Packet
        // Note: The data is ALREADY compressed here. 
        // But VideoFramePacket::serialize will try to compress it AGAIN if we tell it to.
        // We need to change VideoFramePacket::serialize to handle pre-compressed data?
        // OR we just store uncompressed data in VideoFramePacket and let serialize handle it?
        // BUT we want to decide based on compressed size!
        
        // Modification: We'll store the RAW data in the packet, but pass a flag to serialize 
        // effectively saying "I've already decided compression is good".
        // Wait, VideoFramePacket::serialize does the compression.
        // If we want to check size first, we have to do it outside.
        
        // Optimization for this specific plan:
        // We already compressed it to check size. It's wasteful to throw it away and re-compress in serialize.
        // BUT `VideoFramePacket` expects `data` to be valid payload.
        // If we put compressed data in `data`, and tell serialize `use_compression=false`, it works!
        
        is_delta = used_delta;
        
        if is_delta {
            flags |= flags::FLAG_DELTA;
        }
        // We always compress in this new pipeline
        flags |= flags::FLAG_COMPRESS_1; 

        // We construct the packet with the ALREADY COMPRESSED data.
        // And we will call serialize(false) because it's already compressed.
        
        // Logging stats every 60 frames (approx 1 second)
        if self.frame_count % 60 == 0 {
             let raw_size = current_frame.len();
             let compressed_size = best_data.len();
             let ratio = (compressed_size as f64 / raw_size as f64) * 100.0;
             let type_str = if is_delta { "Delta" } else { "Keyframe" };
             tracing::info!(
                 "Frame #{}: {} | Raw: {}B -> Comp: {}B ({:.1}%)", 
                 self.frame_count, type_str, raw_size, compressed_size, ratio
             );
        }

        (VideoFramePacket {
            width,
            height,
            is_delta,
            extra_flags: flags, // Pass flags through
            data: best_data, // Pre-compressed
        }, flags)
    }

    // SIMD-Accelerated XOR
    fn compute_delta_simd(&self, current: &[u8], previous: &[u8]) -> Vec<u8> {
        let len = current.len().min(previous.len());
        let mut delta = vec![0u8; len];
        
        let chunks = len / 16;
        let _remainder = len % 16;
        
        let current_u128: &[u128] = bytemuck::cast_slice(&current[..chunks*16]);
        let previous_u128: &[u128] = bytemuck::cast_slice(&previous[..chunks*16]);
        let delta_u128: &mut [u128] = bytemuck::cast_slice_mut(&mut delta[..chunks*16]);
        
        for i in 0..chunks {
            delta_u128[i] = current_u128[i] ^ previous_u128[i];
        }
        
        // Handle remainder
        for i in (chunks*16)..len {
            delta[i] = current[i] ^ previous[i];
        }
        
        delta
    }
}
