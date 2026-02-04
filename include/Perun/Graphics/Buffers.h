#pragma once

#include <vector>
#include <glad/glad.h>

namespace Perun::Graphics {

// Vertex Buffer
class VertexBuffer {
public:
    VertexBuffer(const void* data, uint32_t size) {
        glCreateBuffers(1, &m_RendererID);
        glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
        glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
    }

    ~VertexBuffer() {
        glDeleteBuffers(1, &m_RendererID);
    }

    void Bind() const { glBindBuffer(GL_ARRAY_BUFFER, m_RendererID); }
    void Unbind() const { glBindBuffer(GL_ARRAY_BUFFER, 0); }

private:
    uint32_t m_RendererID;
};

// Index Buffer
class IndexBuffer {
public:
    IndexBuffer(const uint32_t* data, uint32_t count) : m_Count(count) {
        glCreateBuffers(1, &m_RendererID);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(uint32_t), data, GL_STATIC_DRAW);
    }

    ~IndexBuffer() {
        glDeleteBuffers(1, &m_RendererID);
    }

    void Bind() const { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID); }
    void Unbind() const { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); }
    uint32_t GetCount() const { return m_Count; }

private:
    uint32_t m_RendererID;
    uint32_t m_Count;
};

// Vertex Array
class VertexArray {
public:
    VertexArray() {
        glCreateVertexArrays(1, &m_RendererID);
    }

    ~VertexArray() {
        glDeleteVertexArrays(1, &m_RendererID);
    }

    void Bind() const { glBindVertexArray(m_RendererID); }
    void Unbind() const { glBindVertexArray(0); }

    void AddBuffer(const VertexBuffer& vb) {
        Bind();
        vb.Bind();
        // Assuming tight float layout for now: [x, y, z] or [x, y]
        // Hardcoded for Vec2 (2 floats) for this "MVP"
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (const void*)0);
    }
    
    void SetIndexBuffer(const IndexBuffer& ib) {
        Bind();
        ib.Bind();
    }

private:
    uint32_t m_RendererID;
};

} // namespace Perun::Graphics
