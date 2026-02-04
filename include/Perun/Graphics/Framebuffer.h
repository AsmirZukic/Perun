#pragma once

#include <cstdint>

namespace Perun::Graphics {

struct FramebufferSpecification {
    int Width, Height;
    // We can add formats here later (RGBA8, Depth, etc)
    bool SwapChainTarget = false; // Is this the screen?
};

class Framebuffer {
public:
    virtual ~Framebuffer() = default;

    virtual void Bind() = 0;
    virtual void Unbind() = 0;
    
    virtual void Resize(int width, int height) = 0;
    
    virtual int GetRendererID() const = 0;
    virtual int GetColorAttachmentRendererID() const = 0;
    
    virtual const FramebufferSpecification& GetSpecification() const = 0;

    static Framebuffer* Create(const FramebufferSpecification& spec);
};

} // namespace Perun::Graphics
