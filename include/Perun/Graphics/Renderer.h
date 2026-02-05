#pragma once

#include "Perun/Math/Vector2.h"
#include "Perun/Math/Matrix4.h"

namespace Perun {
    namespace Graphics { class Texture2D; }


class Renderer {
public:
    static void Init();
    static void Shutdown();

    static void BeginScene(const Math::Matrix4& projection);
    static void EndScene();

    static void DrawQuad(const Math::Vector2& position, const Math::Vector2& size, const float color[4]);
    static void DrawQuad(const Math::Vector2& position, const Math::Vector2& size, const Graphics::Texture2D& texture, const float tintColor[4] = nullptr);
    static void DrawCircle(const Math::Vector2& position, float radius, const float color[4], float thickness = 1.0f, float fade = 0.005f);

private:
    struct RendererData;
    static RendererData* s_Data;
};

} // namespace Perun
