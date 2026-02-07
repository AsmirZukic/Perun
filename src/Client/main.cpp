#include <iostream>
#include "Perun/Core/Window.h"
#include "Perun/Graphics/Renderer.h"
#include "Perun/Client/Client.h"
#include <SDL2/SDL.h>

// Simple texture uploader helper (will be moved to Renderer later)
#include <glad/glad.h>

int main(int argc, char* argv[]) {
    // Parse args
    std::string address = "/tmp/perun.sock";
    bool useTcp = false;
    
    for (int i=1; i<argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--tcp") {
            useTcp = true;
            if (i+1 < argc && argv[i+1][0] != '-') {
                address = argv[i+1];
                i++;
            } else {
                address = "127.0.0.1:8080";
            }
        } else if (arg == "--unix") {
            useTcp = false;
             if (i+1 < argc) {
                address = argv[i+1];
                i++;
            }
        }
    }

    Perun::Core::Window window("Perun Client", 640, 480);
    if (!window.Init()) {
        return 1;
    }
    
    Perun::Renderer::Init();
    
    Perun::Client::Client client;
    if (!client.Connect(address, useTcp)) {
        std::cerr << "Failed to connect to server." << std::endl;
        // Don't exit immediately, maybe user wants to retry? 
        // For MVP, exit.
        // window.Shutdown(); // Destructor handles it? Window destructor doesn't seem to call Shutdown based on header?
        // Header says ~Window(), we assume it cleans up.
        // But we called Init().
        return 1;
    }
    
    // Create a GL texture for the screen
    GLuint screenTexture;
    glGenTextures(1, &screenTexture);
    glBindTexture(GL_TEXTURE_2D, screenTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 640, 480, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    
    bool running = true;
    while (running) {
        window.PollEvents();
        if (window.ShouldClose()) {
            running = false;
        }
        
        client.Update();
        
        // Perun::Renderer::Clear(); // Logic moved to BeginScene inside Render
        
        // Bind and update texture
        glActiveTexture(GL_TEXTURE0);
        
        client.UpdateTexture(screenTexture);
        
        // Ensure texture is bound for rendering
        glBindTexture(GL_TEXTURE_2D, screenTexture);
        
        client.Render();
        
        window.SwapBuffers();
    }
    
    client.Disconnect();
    Perun::Renderer::Shutdown();
    // window destructor handles shutdown
    
    return 0;
}
