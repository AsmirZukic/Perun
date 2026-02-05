#pragma once

#include <SDL2/SDL.h>

namespace Perun {

class ImGuiLayer {
public:
    static void Init(void* window, void* context);
    static void Shutdown();

    static void Begin();
    static void End();

    static void OnEvent(const SDL_Event& event);
};

} // namespace Perun
