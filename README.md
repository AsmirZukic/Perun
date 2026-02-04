# Perun Engine

**Perun** is a high-performance, modern C++ 2D Graphics Engine featuring OpenGL 4.5 rendering. It provides a robust foundation for building games and graphical applications with a clean, decoupled architecture.

## Features

- **Rendering**: Batch-ready 2D Quad Rendering using OpenGL 4.5 Core Profile.
- **Windowing**: Cross-platform Window and Input management (via SDL2).
- **Math**: Custom, test-driven standalone Math library (`Vector2`, `Matrix4`).
- **Build System**: Modern CMake build system with `install` support.

## Prerequisites

- **Compiler**: C++20 compliant compiler (`g++`, `clang++`, or `MSVC`).
- **Tools**: `CMake` 3.14+, `Make` or `Ninja`.
- **Libraries**:
    - `SDL2` (Development files)
    - `SDL2_image` (Development files)
    - `OpenGL`

## Building

```bash
# Clone the repository
git clone <repo-url> Perun
cd Perun

# Configure and Build
cmake -S . -B build
cmake --build build

# Run the Sandbox Example
./build/PerunSandbox
```

## Testing

Perun uses **GoogleTest** for verification.

```bash
cd build
ctest --output-on-failure
```

## Usage

Perun is designed to be used as a library.

```cpp
#include "Perun/Core/Window.h"
#include "Perun/Graphics/Renderer.h"

int main() {
    Perun::Core::Window window("My Game", 800, 600);
    window.Init();
    Perun::Renderer::Init();

    while (!window.ShouldClose()) {
        window.PollEvents();
        
        Perun::Renderer::BeginScene(projectionMatrix);
        Perun::Renderer::DrawQuad({0,0}, {1,1}, {1,0,0,1}); // Draw Red Quad
        Perun::Renderer::EndScene();
        
        window.SwapBuffers();
    }
    
    Perun::Renderer::Shutdown();
    return 0;
}
```

## License
MIT