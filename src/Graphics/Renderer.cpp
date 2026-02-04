#include "Perun/Graphics/Renderer.h"
#include "Perun/Graphics/Shader.h"
#include "Perun/Graphics/Buffers.h"
#include <memory>
#include <glad/glad.h>
#include <iostream>

namespace Perun {

struct Renderer::RendererData {
    std::unique_ptr<Graphics::VertexArray> QuadVertexArray;
    std::unique_ptr<Graphics::VertexBuffer> QuadVertexBuffer;
    std::unique_ptr<Graphics::IndexBuffer> QuadIndexBuffer;
    std::unique_ptr<Graphics::Shader> FlatColorShader;
};

Renderer::RendererData* Renderer::s_Data = nullptr;

void Renderer::Init() {
    s_Data = new RendererData();
    s_Data->QuadVertexArray = std::make_unique<Graphics::VertexArray>();

    // Quad Vertices (x, y)
    float vertices[] = {
        -0.5f, -0.5f, 
         0.5f, -0.5f, 
         0.5f,  0.5f, 
        -0.5f,  0.5f
    };

    s_Data->QuadVertexBuffer = std::make_unique<Graphics::VertexBuffer>(vertices, sizeof(vertices));
    
    // Setup Layout (Index 0, 2 floats)
    // Note: Our "AddBuffer" in VertexArray currently hardcodes binding, which is fine for V0.1
    s_Data->QuadVertexArray->AddBuffer(*s_Data->QuadVertexBuffer);

    // Quad Indices
    uint32_t indices[] = { 0, 1, 2, 2, 3, 0 };
    s_Data->QuadIndexBuffer = std::make_unique<Graphics::IndexBuffer>(indices, 6);
    s_Data->QuadVertexArray->SetIndexBuffer(*s_Data->QuadIndexBuffer);

    // Default Shader
    std::string vertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec2 a_Position;
        
        uniform mat4 u_ViewProjection;
        uniform mat4 u_Transform;

        void main() {
            gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 0.0, 1.0);
        }
    )";

    std::string fragmentSrc = R"(
        #version 450 core
        layout(location = 0) out vec4 color;
        
        uniform vec4 u_Color;

        void main() {
            color = u_Color;
        }
    )";

    s_Data->FlatColorShader = std::make_unique<Graphics::Shader>(vertexSrc, fragmentSrc);
}

void Renderer::Shutdown() {
    delete s_Data;
    s_Data = nullptr;
}

void Renderer::BeginScene(const Math::Matrix4& projection) {
    s_Data->FlatColorShader->Bind();
    s_Data->FlatColorShader->SetMat4("u_ViewProjection", projection);
}

void Renderer::EndScene() {
    // Flush if batching
}

void Renderer::DrawQuad(const Math::Vector2& position, const Math::Vector2& size, const float color[4]) {
    s_Data->FlatColorShader->Bind();
    
    // Set Color
    // We don't have SetFloat4 yet in Shader, let's add it or just raw call for speed/time saving? 
    // Wait, Shader class only has SetInt, SetFloat, SetMat4.
    // Let's rely on raw GL for the color uniform locally or update Shader class.
    // Updating Shader class is cleaner. 
    // For now, I'll use glUniform4f directly via the Shader's location cache if it was public, but it's not.
    // I SHOULD update Shader.h. but I am in Renderer.cpp. 
    // Let's assume I will update Shader.h in a moment.
    s_Data->FlatColorShader->SetFloat4("u_Color", color[0], color[1], color[2], color[3]);

    // Calculate Transform
    Math::Matrix4 transform = Math::Matrix4::Translate(position) * Math::Matrix4::Scale(size);
    s_Data->FlatColorShader->SetMat4("u_Transform", transform);

    s_Data->QuadVertexArray->Bind();
    glDrawElements(GL_TRIANGLES, s_Data->QuadIndexBuffer->GetCount(), GL_UNSIGNED_INT, nullptr);
}

} // namespace Perun
