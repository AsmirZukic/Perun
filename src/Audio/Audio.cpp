#include "Perun/Audio/Audio.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <iostream>

namespace Perun {

// Simple global state for the beep (not thread safe for advanced logic, but fine for single tone test)
struct AudioState {
    int Frequency = 440;
    int SamplesLeft = 0;
    int SampleRate = 44100;
    float RunningTime = 0.0f;
};

static AudioState s_AudioState;
static SDL_AudioDeviceID s_DeviceID = 0;

static void AudioCallback(void* userdata, Uint8* stream, int len) {
    int16_t* buffer = (int16_t*)stream;
    int length = len / 2; // 2 bytes per sample (S16)

    for (int i = 0; i < length; i++) {
        if (s_AudioState.SamplesLeft > 0) {
            float time = s_AudioState.RunningTime;
            // Sine wave
            float sample = std::sin(2.0f * M_PI * s_AudioState.Frequency * time);
            buffer[i] = (int16_t)(sample * 3000); // Volume 3000
            
            s_AudioState.RunningTime += 1.0f / s_AudioState.SampleRate;
            s_AudioState.SamplesLeft--;
        } else {
            buffer[i] = 0;
            s_AudioState.RunningTime = 0.0f; // Reset phase often to avoid float drift (optional)
        }
    }
}

bool Audio::Init() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        std::cerr << "Failed to init SDL Audio" << std::endl;
        return false;
    }

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 2048;
    want.callback = AudioCallback;

    s_DeviceID = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s_DeviceID == 0) {
        std::cerr << "Failed to open audio device: " << SDL_GetError() << std::endl;
        return false;
    }

    s_AudioState.SampleRate = have.freq;
    SDL_PauseAudioDevice(s_DeviceID, 0); // Start playing silence
    return true;
}

void Audio::Shutdown() {
    if (s_DeviceID != 0) {
        SDL_CloseAudioDevice(s_DeviceID);
        s_DeviceID = 0;
    }
}

void Audio::PlayTone(int frequency, int durationMs) {
    if (s_DeviceID == 0) return;

    // Lock to update state safely
    SDL_LockAudioDevice(s_DeviceID);
    s_AudioState.Frequency = frequency;
    s_AudioState.SamplesLeft = (durationMs * s_AudioState.SampleRate) / 1000;
    SDL_UnlockAudioDevice(s_DeviceID);
}

} // namespace Perun
