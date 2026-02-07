#include "Perun/C/perun_c.h"

#include "Perun/Core/Window.h"
#include "Perun/Graphics/Renderer.h"
#include "Perun/Graphics/Texture.h"

#include <iostream>

using namespace Perun;

// Opaque struct definitions for C
struct PerunWindow {
    Core::Window* cppWindow;
};

struct PerunTexture {
    Graphics::Texture2D* cppTexture;
};

bool Perun_Init() {
    // Global engine init if needed (currently Renderer::Init is separate)
    return true;
}

void Perun_Shutdown() {
    // Global shutdown
}

PerunWindow* Perun_Window_Create(const char* title, int width, int height) {
    auto* w = new PerunWindow();
    w->cppWindow = new Core::Window(title, width, height);
    return w;
}

bool Perun_Window_Init(PerunWindow* window) {
    if (!window || !window->cppWindow) return false;
    return window->cppWindow->Init();
}

void Perun_Window_Destroy(PerunWindow* window) {
    if (window) {
        delete window->cppWindow;
        delete window;
    }
}

bool Perun_Window_Update(PerunWindow* window) {
    if (!window || !window->cppWindow) return false;
    window->cppWindow->PollEvents();
    window->cppWindow->SwapBuffers();
    return !window->cppWindow->ShouldClose();
}

bool Perun_Window_IsKeyDown(PerunWindow* window, int scancode) {
    if (!window || !window->cppWindow) return false;
    return window->cppWindow->IsKeyDown(scancode);
}

void Perun_Window_SetEventCallback(PerunWindow* window, PerunEventCallback callback, void* userData) {
    if (window && window->cppWindow) {
        window->cppWindow->SetEventCallback([callback, userData](const SDL_Event& e) {
             if (callback) callback((void*)&e, userData);
        });
    }
}

void Perun_Renderer_Init() {
    Renderer::Init();
}

void Perun_Renderer_Shutdown() {
    Renderer::Shutdown();
}

void Perun_Renderer_BeginScene() {
    // Identity matrix projection
    float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    Renderer::BeginScene(identity);
}

void Perun_Renderer_EndScene() {
    Renderer::EndScene();
}

void Perun_Renderer_DrawTexture(float x, float y, float w, float h, PerunTexture* texture) {
    if (texture && texture->cppTexture) {
        float pos[2] = {x, y};
        float size[2] = {w, h};
        Renderer::DrawQuad(pos, size, *texture->cppTexture);
    }
}

PerunTexture* Perun_Texture_Create(int width, int height) {
    auto* t = new PerunTexture();
    t->cppTexture = new Graphics::Texture2D(width, height);
    return t;
}

void Perun_Texture_Destroy(PerunTexture* texture) {
    if (texture) {
        delete texture->cppTexture;
        delete texture;
    }
}

void Perun_Texture_SetData(PerunTexture* texture, const void* data, int sizeBytes) {
    if (texture && texture->cppTexture) {
        texture->cppTexture->SetData(data, (uint32_t)sizeBytes);
    }
}
