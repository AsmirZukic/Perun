#include <gtest/gtest.h>
#include "Perun/Graphics/Framebuffer.h"
#include "Perun/Core/Window.h"

using namespace Perun;
using namespace Perun::Graphics;

class FramebufferTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        s_Window = new Core::Window("TestWindow", 100, 100);
        ASSERT_TRUE(s_Window->Init());
    }

    static void TearDownTestSuite() {
        delete s_Window;
    }

    static Core::Window* s_Window;
};

Core::Window* FramebufferTest::s_Window = nullptr;

TEST_F(FramebufferTest, Creation) {
    FramebufferSpecification spec;
    spec.Width = 800;
    spec.Height = 600;
    
    Framebuffer* fb = Framebuffer::Create(spec);
    ASSERT_NE(fb, nullptr);
    EXPECT_GT(fb->GetRendererID(), 0);
    EXPECT_EQ(fb->GetSpecification().Width, 800);
    
    delete fb;
}

TEST_F(FramebufferTest, Resize) {
    FramebufferSpecification spec;
    spec.Width = 100;
    spec.Height = 100;
    
    Framebuffer* fb = Framebuffer::Create(spec);
    EXPECT_EQ(fb->GetSpecification().Width, 100);
    
    fb->Resize(200, 200);
    EXPECT_EQ(fb->GetSpecification().Width, 200);
    EXPECT_EQ(fb->GetSpecification().Height, 200);
    
    delete fb;
}
