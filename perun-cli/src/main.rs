use clap::{Parser, Subcommand};
use std::process::Stdio;
use tokio::process::Command;
use tokio::signal;
use anyhow::{Result, Context};
use std::path::PathBuf;
use tracing::{info, error};

#[derive(Parser)]
#[command(author, version, about, long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Start a Perun demo
    Start {
        /// Core name (e.g., "nes", "chip8") or path to executable
        name: String,
        /// Path to ROM file
        rom: PathBuf,
        /// Width (required for custom cores)
        #[arg(long)]
        width: Option<u32>,
        /// Height (required for custom cores)
        #[arg(long)]
        height: Option<u32>,
    },
    /// Build all components
    Build,
}

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt::init();

    let cli = Cli::parse();

    match &cli.command {
        Commands::Start { name, rom, width, height } => {
            start_demo(name, rom, *width, *height).await?;
        }
        Commands::Build => {
            build_components().await?;
        }
    }

    Ok(())
}

async fn build_components() -> Result<()> {
    info!("Building Perun components...");
    
    // Build Server and Cores
    let status = Command::new("cargo")
        .args(&["build", "--release", "--bin", "perun-server", "--bin", "tetanes-perun"])
        .status()
        .await?;

    if !status.success() {
        anyhow::bail!("Failed to build server components");
    }

    // Build Web Client
    info!("Building WASM Client...");
    let status = Command::new("wasm-pack")
        .current_dir("perun-client")
        .args(&["build", "--target", "web", "--release"])
        .status()
        .await?;

    if !status.success() {
        anyhow::bail!("Failed to build WASM client");
    }

    // Copy to perun-client/www/pkg
    std::fs::create_dir_all("perun-client/www/pkg")?;
    let status = Command::new("cp")
        .args(&["-r", "perun-client/pkg", "perun-client/www/"])
        .status()
        .await?;
     
    if !status.success() {
        anyhow::bail!("Failed to copy pkg to web directory");
    }

    info!("Build complete!");
    Ok(())
}

async fn start_demo(core_name: &str, rom: &PathBuf, width_opt: Option<u32>, height_opt: Option<u32>) -> Result<()> {
    // 1. Build first (ensure up to date)
    build_components().await?;

    // 2. Kill stale processes
    let _ = Command::new("pkill").args(&["-f", "perun-server"]).output().await;
    let _ = Command::new("pkill").args(&["-f", "tetanes-perun"]).output().await;
    let _ = Command::new("pkill").args(&["-f", "cli.py"]).output().await;

    // 3. Resolve Configuration
    let (cmd, args, width, height, shm_path) = match core_name {
        "nes" => (
            "./target/release/tetanes-perun", 
            vec![rom.to_string_lossy().to_string()], 
            256, 240, 
            "/dev/shm/perun_tetanes".to_string()
        ),
        "chip8" => {
             let cwd = std::env::current_dir()?;
             let python_path = cwd.join("examples/python/Chip8_Python/src"); // Adjusted path
             (
                "python3",
                vec![
                    "examples/python/Chip8_Python/src/chip8/cli.py".to_string(),
                    rom.to_string_lossy().to_string(),
                    "--shm".to_string(),
                    "/dev/shm/perun_chip8".to_string()
                ],
                64, 32,
                "/dev/shm/perun_chip8".to_string()
             )
        },
        custom => {
             // Defaults or overrides
             let w = width_opt.ok_or_else(|| anyhow::anyhow!("Width required for custom core"))?;
             let h = height_opt.ok_or_else(|| anyhow::anyhow!("Height required for custom core"))?;
             let shm = format!("/dev/shm/perun_{}", custom);
             (custom, vec![rom.to_string_lossy().to_string()], w, h, shm)
        }
    };

    // Override if provided (even for known cores, though unusual)
    let width = width_opt.unwrap_or(width);
    let height = height_opt.unwrap_or(height);

    // 4. Start Server
    info!("Starting Perun Server for {} ({}x{})...", core_name, width, height);

    let mut server = Command::new("./target/release/perun-server")
        .args(&[
            "--tcp", ":8081", 
            "--ws", ":9002", 
            "--shm", &shm_path, 
            "--width", &width.to_string(), 
            "--height", &height.to_string()
        ])
        .stdout(Stdio::inherit())
        .stderr(Stdio::inherit())
        .kill_on_drop(true)
        .spawn()
        .context("Failed to spawn server")?;

    // 5. Start Web Server
    info!("Starting Web Server on http://localhost:8000");
    let web_route = warp::fs::dir("perun-client/www");
    let server_future = warp::serve(web_route).run(([0, 0, 0, 0], 8000));
    let web_server_handle = tokio::spawn(server_future);

    // Wait for server init
    tokio::time::sleep(tokio::time::Duration::from_secs(1)).await;

    // 6. Start Core
    info!("Starting Emulator Core: {}", cmd);
    
    let mut core_cmd = Command::new(cmd);
    core_cmd.args(&args)
        .stdout(Stdio::inherit())
        .stderr(Stdio::inherit())
        .kill_on_drop(true);
        
    // Special env for chip8 python
    if core_name == "chip8" {
        let cwd = std::env::current_dir()?;
        let python_path = cwd.join("examples/python/Chip8_Python/src");
        core_cmd.env("PYTHONPATH", python_path);
    }

    let mut core_process = core_cmd.spawn().context(format!("Failed to spawn core '{}'", cmd))?;

    info!("Perun Demo Running. Press Ctrl+C to stop.");

    // 7. Handle Ctrl+C
    tokio::select! {
        _ = signal::ctrl_c() => {
            info!("Ctrl+C received, shutting down...");
        }
        _ = server.wait() => {
            error!("Server exited unexpectedly");
        }
        _ = core_process.wait() => {
            error!("Core exited unexpectedly");
        }
        _ = web_server_handle => {
            error!("Web server exited unexpectedly");
        }
    }

    // Explicit kill on exit (though kill_on_drop handles it mostly)
    let _ = server.kill().await;
    let _ = core_process.kill().await;

    Ok(())
}
