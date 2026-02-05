#include "Perun/Graphics/Texture.h"
#include <glad/glad.h>
#include <iostream>

namespace Perun::Graphics {

Texture2D::Texture2D(uint32_t width, uint32_t height)
    : m_Width(width), m_Height(height) {
    
    m_InternalFormat = GL_RGBA8;
    m_DataFormat = GL_RGBA;

    glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
    glTextureStorage2D(m_RendererID, 1, m_InternalFormat, m_Width, m_Height);

    glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

Texture2D::~Texture2D() {
    glDeleteTextures(1, &m_RendererID);
}

void Texture2D::SetData(void* data, uint32_t size) {
    uint32_t bpp = m_DataFormat == GL_RGBA ? 4 : 3;
    if (size != m_Width * m_Height * bpp) {
        std::cerr << "Data size must be entire texture!" << std::endl;
        return;
    }

    glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, m_DataFormat, GL_UNSIGNED_BYTE, data);
}

void Texture2D::Bind(uint32_t slot) const {
    glBindTextureUnit(slot, m_RendererID);
}

} // namespace Perun::Graphics
