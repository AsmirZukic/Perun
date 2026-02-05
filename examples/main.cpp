#include <iostream>
#include <glad/glad.h>
#include <vector>
#include <random>
#include <imgui.h>
#include "Perun/Core/Window.h"
#include "Perun/Graphics/Renderer.h"
#include "Perun/Graphics/Texture.h"
#include "Perun/Math/Matrix4.h"
#include "Perun/ImGui/ImGuiLayer.h"

#include "Perun/Audio/Audio.h"

int main(int argc, char* argv[]) {
    Perun::Core::Window window("Perun Emulator Test", 800, 600);
    
    if (!window.Init()) {
        return -1;
    }
    
    Perun::Renderer::Init();
    if (!Perun::Audio::Init()) {
        std::cerr << "Audio Init Failed!" << std::endl;
    }

    Perun::ImGuiLayer::Init(window.GetNativeWindow(), window.GetContext());

    // Create a "Screen" Texture (64x32)
    const int texWidth = 64;
    const int texHeight = 32;
    Perun::Graphics::Texture2D screenTexture(texWidth, texHeight);
    std::vector<uint32_t> pixels(texWidth * texHeight);

    float posX = 0.0f;
    float posY = 0.0f;
    bool showDemoWindow = true;

    // Loop
    while (!window.ShouldClose()) {
        Perun::ImGuiLayer::Begin();
        
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            Perun::ImGuiLayer::OnEvent(event);
            if (event.type == SDL_QUIT) {
                 exit(0);
            }
        }
        
        bool moved = false;
        if (window.IsKeyDown(SDL_SCANCODE_RIGHT)) { posX += 0.01f; moved = true; }
        if (window.IsKeyDown(SDL_SCANCODE_LEFT)) { posX -= 0.01f; moved = true; }
        if (window.IsKeyDown(SDL_SCANCODE_UP)) { posY += 0.01f; moved = true; }
        if (window.IsKeyDown(SDL_SCANCODE_DOWN)) { posY -= 0.01f; moved = true; }
        
        if (moved) {
            Perun::Audio::PlayTone(440, 50); // Beep on move
        }

        // Dynamic Texture Update
        for (auto& pixel : pixels) {
             uint8_t r = rand() % 255;
             pixel = 0xFF000000 | r | (r << 8) | (r << 16); // Greyscale noise
        }
        screenTexture.SetData(pixels.data(), pixels.size() * sizeof(uint32_t));

        // Render Game
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        Perun::Renderer::BeginScene(Perun::Math::Matrix4::Identity());
        Perun::Renderer::DrawQuad({posX, posY}, {1.0f, 0.5f}, screenTexture);
        Perun::Renderer::EndScene();
        
        // Render UI
        if (showDemoWindow) ImGui::ShowDemoWindow(&showDemoWindow);
        
        ImGui::Begin("Emulator Control");
        ImGui::Text("Status: Running");
        ImGui::SliderFloat("Position X", &posX, -1.0f, 1.0f);
        if (ImGui::Button("Beep")) {
            Perun::Audio::PlayTone(880, 100);
        }
        ImGui::End();

        Perun::ImGuiLayer::End(); // Renders ImGui
        window.SwapBuffers();
    }

    Perun::ImGuiLayer::Shutdown();
    Perun::Audio::Shutdown();
    Perun::Renderer::Shutdown();
    return 0;
}
