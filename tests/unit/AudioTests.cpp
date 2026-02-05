#include <gtest/gtest.h>
#include <SDL2/SDL.h>
#include "Perun/Audio/Audio.h"

// Note: Audio Init depends on SDL hardware access. 
// In a real CI environment, we might need a dummy driver.
// SDL_setenv("SDL_AUDIODRIVER", "dummy", 1); 

class AudioTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Force dummy driver for reliability in tests
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    }
};

TEST_F(AudioTest, Initialization) {
    bool success = Perun::Audio::Init();
    EXPECT_TRUE(success);
    Perun::Audio::Shutdown();
}

TEST_F(AudioTest, PlayToneDoesNotCrash) {
    if (Perun::Audio::Init()) {
        Perun::Audio::PlayTone(440, 100);
        Perun::Audio::Shutdown();
        SUCCEED();
    }
}
