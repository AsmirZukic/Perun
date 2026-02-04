#include "Perun/Graphics/Framebuffer.h"
#include <glad/glad.h>
#include <iostream>

namespace Perun::Graphics {

class OpenGLFramebuffer : public Framebuffer {
public:
    OpenGLFramebuffer(const FramebufferSpecification& spec)
        : m_Specification(spec) {
        Invalidate();
    }

    virtual ~OpenGLFramebuffer() {
        if (m_RendererID) {
            glDeleteFramebuffers(1, &m_RendererID);
            glDeleteTextures(1, &m_ColorAttachment);
            glDeleteRenderbuffers(1, &m_DepthAttachment);
        }
    }

    void Invalidate() {
        if (m_RendererID) {
            glDeleteFramebuffers(1, &m_RendererID);
            glDeleteTextures(1, &m_ColorAttachment);
            glDeleteRenderbuffers(1, &m_DepthAttachment);
        }
        
        glCreateFramebuffers(1, &m_RendererID);
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
        
        glCreateTextures(GL_TEXTURE_2D, 1, &m_ColorAttachment);
        glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Specification.Width, m_Specification.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorAttachment, 0);
        
        // Depth/Stencil
        // ... omitted for now if not needed, but good to have.
        
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cout << "Framebuffer is incomplete!" << std::endl;
            
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    
    virtual void Bind() override {
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
        glViewport(0, 0, m_Specification.Width, m_Specification.Height);
    }

    virtual void Unbind() override {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    
    virtual void Resize(int width, int height) override {
        if (width == 0 || height == 0 || (width == m_Specification.Width && height == m_Specification.Height))
            return;
            
        m_Specification.Width = width;
        m_Specification.Height = height;
        Invalidate();
    }
    
    virtual int GetRendererID() const override { return (int)m_RendererID; }
    virtual int GetColorAttachmentRendererID() const override { return (int)m_ColorAttachment; }
    
    virtual const FramebufferSpecification& GetSpecification() const override { return m_Specification; }

private:
    uint32_t m_RendererID = 0;
    uint32_t m_ColorAttachment = 0;
    uint32_t m_DepthAttachment = 0;
    FramebufferSpecification m_Specification;
};

Framebuffer* Framebuffer::Create(const FramebufferSpecification& spec) {
    return new OpenGLFramebuffer(spec);
}

} // namespace Perun::Graphics
