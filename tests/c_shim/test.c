#include "Perun/C/perun_c.h"
#include <stdio.h>
#include <SDL2/SDL.h> // For SDL_Delay

int main(int argc, char* argv[]) {
    printf("Initializing Perun C-API...\n");
    if (!Perun_Init()) {
        printf("Failed to init Perun\n");
        return 1;
    }

    printf("Creating Window...\n");
    PerunWindow* window = Perun_Window_Create("Perun C-API Test", 640, 480);
    if (!Perun_Window_Init(window)) {
         printf("Failed to init Window\n");
         return 1;
    }

    Perun_Renderer_Init();

    printf("Running Loop for 60 frames...\n");
    for (int i = 0; i < 60; ++i) {
        Perun_Renderer_BeginScene();
        // Clear screen? (Renderer hides glClear calls, should likely expose Clear)
        Perun_Renderer_EndScene();

        if (!Perun_Window_Update(window)) {
            break;
        }
        SDL_Delay(16);
    }
    
    printf("Shutting Down...\n");
    Perun_Renderer_Shutdown();
    Perun_Window_Destroy(window);
    Perun_Shutdown();
    return 0;
}
