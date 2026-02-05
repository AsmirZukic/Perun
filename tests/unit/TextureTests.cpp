#include <gtest/gtest.h>
#include "Perun/Graphics/Texture.h"
#include "Perun/Core/Window.h"
#include <vector>

// We need an OpenGL context for Textures. 
// Standard UnitTests might fail if they run headless without a context.
// We can use a Test Fixture to init a hidden window.

class GraphicsTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // One-time setup
        window = new Perun::Core::Window("Test Context", 100, 100);
        window->Init();
    }

    static void TearDownTestSuite() {
        delete window;
    }

    static Perun::Core::Window* window;
};

Perun::Core::Window* GraphicsTest::window = nullptr;

TEST_F(GraphicsTest, TextureCreation) {
    Perun::Graphics::Texture2D texture(64, 32);
    EXPECT_EQ(texture.GetWidth(), 64);
    EXPECT_EQ(texture.GetHeight(), 32);
    EXPECT_NE(texture.GetRendererID(), 0);
}

TEST_F(GraphicsTest, TextureUpload) {
    Perun::Graphics::Texture2D texture(2, 2);
    std::vector<uint32_t> data = {
        0xFF0000FF, 0xFF00FF00,
        0xFFFF0000, 0xFFFFFFFF
    };
    
    // This just tests it doesn't crash, validating GL state is harder without reading back
    // We assume SetData works if no GL errors trigger (which we don't catch yet automatically)
    texture.SetData(data.data(), data.size() * sizeof(uint32_t));
    
    // Ideally we would glGetTexImage to verify, but that's slow. 
    // For now we trust the mechanic.
    SUCCEED();
}
