#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handles
typedef struct PerunWindow PerunWindow;
typedef struct PerunTexture PerunTexture;

// Core
bool Perun_Init();
void Perun_Shutdown();

// Window
PerunWindow* Perun_Window_Create(const char* title, int width, int height);
void Perun_Window_Destroy(PerunWindow* window);
bool Perun_Window_Update(PerunWindow* window); // Returns false if should close
bool Perun_Window_Init(PerunWindow* window);

// Graphics (Context must be active)
void Perun_Renderer_Init();
void Perun_Renderer_Shutdown();
void Perun_Renderer_BeginScene();
void Perun_Renderer_EndScene();
void Perun_Renderer_DrawTexture(float x, float y, float w, float h, PerunTexture* texture);

// Texture
PerunTexture* Perun_Texture_Create(int width, int height);
void Perun_Texture_Destroy(PerunTexture* texture);
void Perun_Texture_SetData(PerunTexture* texture, const void* data, int sizeBytes);

#ifdef __cplusplus
}
#endif
