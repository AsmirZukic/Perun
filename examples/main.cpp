#include <iostream>
#include <glad/glad.h>
#include <vector>
#include <random>
#include "Perun/Core/Window.h"
#include "Perun/Graphics/Renderer.h"
#include "Perun/Graphics/Texture.h"
#include "Perun/Math/Matrix4.h"

int main(int argc, char* argv[]) {
    Perun::Core::Window window("Perun Emulator Test", 800, 600);
    
    if (!window.Init()) {
        return -1;
    }
    
    Perun::Renderer::Init();

    // Create a "Screen" Texture (64x32) - Common Chip-8 resolution
    const int texWidth = 64;
    const int texHeight = 32;
    Perun::Graphics::Texture2D screenTexture(texWidth, texHeight);
    std::vector<uint32_t> pixels(texWidth * texHeight);

    float posX = 0.0f;
    float posY = 0.0f;

    // Loop
    while (!window.ShouldClose()) {
        window.PollEvents();

        // Input Test
        if (window.IsKeyDown(SDL_SCANCODE_RIGHT)) posX += 0.01f;
        if (window.IsKeyDown(SDL_SCANCODE_LEFT)) posX -= 0.01f;
        if (window.IsKeyDown(SDL_SCANCODE_UP)) posY += 0.01f;
        if (window.IsKeyDown(SDL_SCANCODE_DOWN)) posY -= 0.01f;

        // Dynamic Texture Test (Random Noise)
        for (auto& pixel : pixels) {
            uint8_t r = rand() % 255;
            uint8_t g = rand() % 255;
            uint8_t b = rand() % 255;
            pixel = 0xFF000000 | (b << 16) | (g << 8) | r; // ABGR for little endian / GL_RGBA? 
            // Wait, GL_RGBA expects R, G, B, A in byte order.
            // On Little Endian uint32: 0xAABBGGRR
            // So: r | (g<<8) | (b<<16) | (a<<24)
            // Let's use 0xFF000000 | b << 16 | g << 8 | r for now and see.
            // Actually let's use explicit bytes if unsure, but vector is uint32.
            // Let's just do random color.
        }
        screenTexture.SetData(pixels.data(), pixels.size() * sizeof(uint32_t));

        // Render
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        Perun::Renderer::BeginScene(Perun::Math::Matrix4::Identity());
        
        // Draw Emulator Screen
        // Scale it up to be visible
        Perun::Renderer::DrawQuad({posX, posY}, {1.0f, 0.5f}, screenTexture);
        
        Perun::Renderer::EndScene();

        window.SwapBuffers();
    }

    Perun::Renderer::Shutdown();
    return 0;
}
