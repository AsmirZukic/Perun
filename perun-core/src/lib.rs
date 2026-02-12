//! Perun Core SDK
//!
//! Provides the runtime and traits for implementing Perun cores.

use std::error::Error;
use std::fs::OpenOptions;
use std::time::{Duration, Instant};
use memmap2::MmapMut;
use perun_shm::ShmState;
use log::{info, error};

/// Trait that all Perun cores must implement
pub trait PerunCore {
    /// Initialize the core with the given ROM path and configuration
    fn new(rom_path: &str, width: u32, height: u32) -> Result<Self, Box<dyn Error>> where Self: Sized;

    /// Update the core for one frame
    /// 
    /// # Arguments
    /// * `input` - The current input state flags
    /// * `video` - The video buffer to write to (RGBA)
    /// * `audio` - The audio buffer to write to (not yet used)
    fn update(&mut self, input: u32, video: &mut [u8], audio: &mut [i16]) -> Result<(), Box<dyn Error>>;
}

/// Run a Perun core
///
/// This function handles:
/// - Command line argument parsing
/// - SHM initialization and mapping
/// - The main emulation loop (synchronization, throttling, FPS logging)
pub fn run<C: PerunCore>(core_name: &str, width: u32, height: u32) -> Result<(), Box<dyn Error>> {
    env_logger::init();
    
    // 1. Argument Parsing
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        eprintln!("Usage: {} <rom_path>", args[0]);
        std::process::exit(1);
    }
    let rom_path = &args[1];

    info!("Starting Perun Core '{}' with ROM: {}", core_name, rom_path);

    // 2. Core Initialization
    let mut core = C::new(rom_path, width, height)?;

    // 3. SHM Setup
    let shm_path = format!("/dev/shm/perun_{}", core_name);
    info!("Connecting to SHM at {}", shm_path);

    let file = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .open(&shm_path)?;

    let size = std::mem::size_of::<ShmState>() as u64;
    file.set_len(size)?;
    
    let mut mmap = unsafe { MmapMut::map_mut(&file)? };
    let state = unsafe { &mut *(mmap.as_mut_ptr() as *mut ShmState) };

    // Initialize SHM state
    state.width = width;
    state.height = height;
    state.pitch = width * 4;
    state.status_flag.store(ShmState::STATUS_IDLE, std::sync::atomic::Ordering::Release);

    // 4. Main Loop
    let mut frame_count = 0;
    let mut last_second = Instant::now();
    let mut audio_buffer = Vec::new(); // Placeholder

    loop {
        let frame_start = Instant::now();

        // Read Inputs (Relaxed load is fine for input)
        let input = state.input_flags.load(std::sync::atomic::Ordering::Relaxed);

        // Check Status
        let status = state.status_flag.load(std::sync::atomic::Ordering::Acquire);
        
        if status == ShmState::STATUS_IDLE {
            // Lock for writing
            state.status_flag.store(ShmState::STATUS_CORE_WRITING, std::sync::atomic::Ordering::Release);

            // Get video buffer slice
            let buffer_len = (width * height * 4) as usize;
            // Ensure we don't panic if SHM is weird, though it's fixed size array in struct
            let pixels = &mut state.frame_buffer[..buffer_len];

            // Run Core
            if let Err(e) = core.update(input, pixels, &mut audio_buffer) {
                error!("Core update error: {}", e);
                break;
            }

            // Mark ready
            state.status_flag.store(ShmState::STATUS_FRAME_READY, std::sync::atomic::Ordering::Release);
            frame_count += 1;
        } else {
            // Yield if not ready (server is reading or busy)
            std::thread::yield_now();
        }

        // FPS Throttling (Target 60 FPS = ~16.67ms)
        let elapsed = frame_start.elapsed();
        if elapsed < Duration::from_micros(16667) {
             std::thread::sleep(Duration::from_micros(16667) - elapsed);
        }

        // Log FPS
        if last_second.elapsed() >= Duration::from_secs(1) {
            info!("FPS: {}", frame_count);
            frame_count = 0;
            last_second = Instant::now();
        }
    }

    Ok(())
}
