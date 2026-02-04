#include <iostream>
#include <glad/glad.h>
#include "Perun/Core/Window.h"
#include "Perun/Graphics/Renderer.h"
#include "Perun/Math/Matrix4.h"

int main(int argc, char* argv[]) {
    Perun::Core::Window window("Perun Engine", 800, 600);
    
    if (!window.Init()) {
        return -1;
    }
    
    Perun::Renderer::Init();

    // Loop
    while (!window.ShouldClose()) {
        window.PollEvents();

        // Render
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        Perun::Renderer::BeginScene(Perun::Math::Matrix4::Identity());
        
        // Draw Red Center Quad
        float red[] = {1.0f, 0.0f, 0.0f, 1.0f};
        Perun::Renderer::DrawQuad({0.0f, 0.0f}, {0.5f, 0.5f}, red);
        
        // Draw Blue Top-Right Quad
        float blue[] = {0.0f, 0.0f, 1.0f, 1.0f};
        Perun::Renderer::DrawQuad({0.5f, 0.5f}, {0.2f, 0.2f}, blue);

        Perun::Renderer::EndScene();

        window.SwapBuffers();
    }

    Perun::Renderer::Shutdown();
    return 0;
}
