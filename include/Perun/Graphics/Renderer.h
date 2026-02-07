#pragma once

namespace Perun {
    namespace Graphics { class Texture2D; }


class Renderer {
public:
    static void Init();
    static void Shutdown();

    static void BeginScene(const float projection[16]);
    static void EndScene();

    static void DrawQuad(const float position[2], const float size[2], const float color[4]);
    static void DrawQuad(const float position[2], const float size[2], const Graphics::Texture2D& texture, const float tintColor[4] = nullptr);
    static void DrawCircle(const float position[2], float radius, const float color[4], float thickness = 1.0f, float fade = 0.005f);

private:
    struct RendererData;
    static RendererData* s_Data;
};

} // namespace Perun
