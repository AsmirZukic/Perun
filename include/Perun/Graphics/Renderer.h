#pragma once

#include "Perun/Math/Vector2.h"
#include "Perun/Math/Matrix4.h"

namespace Perun {

class Renderer {
public:
    static void Init();
    static void Shutdown();

    static void BeginScene(const Math::Matrix4& projection);
    static void EndScene();

    static void DrawQuad(const Math::Vector2& position, const Math::Vector2& size, const float color[4]);
    // Future: DrawQuad with texture, rotation, etc.

private:
    struct RendererData;
    static RendererData* s_Data;
};

} // namespace Perun
