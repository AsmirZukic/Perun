#include <gtest/gtest.h>
#include "Perun/Graphics/Renderer.h"
#include "Perun/Core/Window.h"

using namespace Perun;

class RendererTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // We need an OpenGL context for the Renderer to work.
        // Create a hidden window.
        s_Window = new Core::Window("TestWindow", 100, 100);
        ASSERT_TRUE(s_Window->Init());
    }

    static void TearDownTestSuite() {
        delete s_Window;
    }

    static Core::Window* s_Window;
};

Core::Window* RendererTest::s_Window = nullptr;

TEST_F(RendererTest, Lifecycle) {
    // Just verify we can Init and Shutdown without crashing
    Renderer::Init();
    
    // Identity matrix for projection
    float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    
    // Draw something (should not crash)
    Renderer::BeginScene(identity);
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float pos1[2] = {0.0f, 0.0f};
    float size1[2] = {1.0f, 1.0f};
    float pos2[2] = {0.5f, 0.5f};
    Renderer::DrawQuad(pos1, size1, color);
    Renderer::DrawCircle(pos2, 0.5f, color);
    Renderer::EndScene();

    Renderer::Shutdown();
}
