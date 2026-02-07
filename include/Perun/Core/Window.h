#pragma once

#include <string>
#include <SDL2/SDL.h>
#include <functional>

namespace Perun::Core {

class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    bool Init();
    void SwapBuffers();
    void PollEvents();
    void SetEventCallback(const std::function<void(const SDL_Event&)>& callback) { m_EventCallback = callback; }
    bool ShouldClose() const { return m_ShouldClose; }
    
    int GetWidth() const { return m_Width; }

    int GetHeight() const { return m_Height; }

    std::pair<int, int> GetMousePosition() const;
    bool IsMouseButtonDown(int button) const;
    bool IsKeyDown(int scancode) const;

    SDL_Window* GetNativeWindow() const { return m_Window; }
    void* GetContext() const { return m_Context; }

private:
    std::string m_Title;
    int m_Width;
    int m_Height;
    bool m_ShouldClose = false;

    SDL_Window* m_Window = nullptr;
    SDL_GLContext m_Context = nullptr;

    std::function<void(const SDL_Event&)> m_EventCallback;
};

} // namespace Perun::Core
