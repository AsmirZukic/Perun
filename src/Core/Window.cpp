#include "Perun/Core/Window.h"
#include <glad/glad.h>
#include <iostream>

namespace Perun::Core {

Window::Window(const std::string& title, int width, int height)
    : m_Title(title), m_Width(width), m_Height(height) {
}

Window::~Window() {
    if (m_Context) SDL_GL_DeleteContext(m_Context);
    if (m_Window) SDL_DestroyWindow(m_Window);
    SDL_Quit();
}

bool Window::Init() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
        return false;
    }

    // OpenGL 4.5 Core Profile
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    m_Window = SDL_CreateWindow(m_Title.c_str(), 
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
        m_Width, m_Height, 
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    if (!m_Window) {
        std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
        return false;
    }

    m_Context = SDL_GL_CreateContext(m_Window);
    if (!m_Context) {
        std::cerr << "Failed to create OpenGL context: " << SDL_GetError() << std::endl;
        return false;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return false;
    }

    std::cout << "OpenGL Info:" << std::endl;
    std::cout << "  Vendor: " << glGetString(GL_VENDOR) << std::endl;
    std::cout << "  Renderer: " << glGetString(GL_RENDERER) << std::endl;
    std::cout << "  Version: " << glGetString(GL_VERSION) << std::endl;

    glViewport(0, 0, m_Width, m_Height);
    
    // Enable Alpha Blending for Circles/Text
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    return true;
}

void Window::SwapBuffers() {
    SDL_GL_SwapWindow(m_Window);
}

void Window::PollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (m_EventCallback) {
            m_EventCallback(event);
        }

        if (event.type == SDL_QUIT) {
            m_ShouldClose = true;
        }
        else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
            m_Width = event.window.data1;
            m_Height = event.window.data2;
            glViewport(0, 0, m_Width, m_Height);
        }
    }
}



std::pair<int, int> Window::GetMousePosition() const {
    int x, y;
    SDL_GetMouseState(&x, &y);
    return {x, y};
}

bool Window::IsMouseButtonDown(int button) const {
    int x, y;
    uint32_t buttons = SDL_GetMouseState(&x, &y);
    return (buttons & SDL_BUTTON(button)) != 0;
}

bool Window::IsKeyDown(int scancode) const {
    const Uint8* state = SDL_GetKeyboardState(nullptr);
    return state[scancode];
}

} // namespace Perun::Core
