#pragma once

#include <string>
#include <SDL2/SDL.h>

namespace Perun::Core {

class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    bool Init();
    void SwapBuffers();
    void PollEvents();
    bool ShouldClose() const { return m_ShouldClose; }
    
    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }

private:
    std::string m_Title;
    int m_Width;
    int m_Height;
    bool m_ShouldClose = false;

    SDL_Window* m_Window = nullptr;
    SDL_GLContext m_Context = nullptr;
};

} // namespace Perun::Core
