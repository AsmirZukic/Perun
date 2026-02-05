#pragma once

#include <cstdint>

namespace Perun {

class Audio {
public:
    static bool Init();
    static void Shutdown();

    static void PlayTone(int frequency, int durationMs);
};

} // namespace Perun
