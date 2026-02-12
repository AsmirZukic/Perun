use std::fs::OpenOptions;
use std::path::Path;
use memmap2::MmapMut;
use perun_shm::ShmState;
use tracing::{info, error};
use std::sync::atomic::AtomicU32;

pub struct ShmHost {
    mmap: MmapMut,
    state: *mut ShmState,
}

unsafe impl Send for ShmHost {}
unsafe impl Sync for ShmHost {}

impl ShmHost {
    pub fn new(path: &str, width: u32, height: u32) -> std::io::Result<Self> {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .open(path)?;

        let size = std::mem::size_of::<ShmState>() as u64;
        file.set_len(size)?;

        let mut mmap = unsafe { MmapMut::map_mut(&file)? };
        
        // Initialize the SHM state
        let state_ptr = mmap.as_mut_ptr() as *mut ShmState;
        
        // Initialize fields directly to avoid stack overflow from creating ShmState on stack
        unsafe {
            let state = &mut *state_ptr;
            
            // Initialize atomics directly
            // We can't use simple assignment if the destination is uninitialized memory that needs drop,
            // but AtomicU32 and primitive types don't implement Drop, so assignment is memory-safe 
            // even if previous content was garbage.
            // Using std::ptr::write is the most correct way to say "initialize this memory".
            
            std::ptr::write(&mut state.status_flag, AtomicU32::new(ShmState::STATUS_IDLE));
            std::ptr::write(&mut state.input_flags, AtomicU32::new(0));
            
            state.width = width;
            state.height = height;
            state.pitch = width * 4;
            
            // Frame buffer is initialized to 0 by OS for new file/mapping usually, 
            // or we just leave it as is if it's existing. 
            // No need to zero 64MB explicitly if we don't assume security scope here.
        }

        info!("SHM initialized at {}, size: {} bytes", path, size);

        Ok(Self {
            mmap,
            state: state_ptr,
        })
    }

    pub fn read_frame_into(&self, buffer: &mut Vec<u8>) -> Option<(u32, u32)> {
        unsafe {
            let status = (*self.state).status_flag.load(std::sync::atomic::Ordering::Acquire);
            if status == ShmState::STATUS_FRAME_READY {
                // Mark as reading
                (*self.state).status_flag.store(ShmState::STATUS_SERVER_READING, std::sync::atomic::Ordering::Release);
                
                // Resize buffer if needed
                let width = (*self.state).width;
                let height = (*self.state).height;
                let expected_size = (width * height * 4) as usize; // Assuming RGBA
                
                if buffer.len() != expected_size {
                    buffer.resize(expected_size, 0);
                }

                // Copy data
                let src = &(*self.state).frame_buffer[..expected_size];
                buffer.copy_from_slice(src);

                // Mark as idle (done reading)
                (*self.state).status_flag.store(ShmState::STATUS_IDLE, std::sync::atomic::Ordering::Release);
                return Some((width, height));
            }
        }
        None
    }

    pub fn write_inputs(&self, buttons: u16) {
        unsafe {
            // Space Invaders expects:
            // Bit 0: Coin
            // Bit 1: P2 Start
            // Bit 2: P1 Start
            // Bit 3: ?
            // Bit 4: P1 Shot
            // Bit 5: P1 Left
            // Bit 6: P1 Right
            // Bit 7: ?
            
            // Perun Protocol might be different. 
            // For now, let's assume 1:1 mapping or map in server.
            // Using relaxed ordering as inputs are sampled per frame.
            (*self.state).input_flags.store(buttons as u32, std::sync::atomic::Ordering::Relaxed);
        }
    }
}
