#include "Perun/Graphics/Renderer.h"
#include "Perun/Graphics/Shader.h"
#include "Perun/Graphics/Buffers.h"
#include <memory>
#include <glad/glad.h>
#include "Perun/Graphics/Texture.h"
#include <iostream>
#include <cstring>
#include <cmath>

namespace Perun {

// Simple matrix and vector helpers to avoid Math library dependency
namespace {
    void MatrixIdentity(float out[16]) {
        memset(out, 0, sizeof(float) * 16);
        out[0] = out[5] = out[10] = out[15] = 1.0f;
    }

    void MatrixTranslate(float out[16], const float pos[2]) {
        MatrixIdentity(out);
        out[12] = pos[0];
        out[13] = pos[1];
    }

    void MatrixScale(float out[16], const float scale[2]) {
        MatrixIdentity(out);
        out[0] = scale[0];
        out[5] = scale[1];
    }

    void MatrixMultiply(float out[16], const float a[16], const float b[16]) {
        float result[16];
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                result[i * 4 + j] = 0;
                for (int k = 0; k < 4; k++) {
                    result[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
                }
            }
        }
        memcpy(out, result, sizeof(float) * 16);
    }
}

struct Renderer::RendererData {
    std::unique_ptr<Graphics::VertexArray> QuadVertexArray;
    std::unique_ptr<Graphics::VertexBuffer> QuadVertexBuffer;
    std::unique_ptr<Graphics::IndexBuffer> QuadIndexBuffer;
    std::unique_ptr<Graphics::Shader> FlatColorShader;
    std::unique_ptr<Graphics::Shader> TextureShader;
    std::unique_ptr<Graphics::Shader> CircleShader;
    float ViewProjection[16];
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

    // Texture Shader
    std::string textureVertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec2 a_Position;
        
        uniform mat4 u_ViewProjection;
        uniform mat4 u_Transform;
        
        out vec2 v_TexCoord;

        void main() {
            v_TexCoord = a_Position + 0.5;
            gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 0.0, 1.0);
        }
    )";

    std::string textureFragmentSrc = R"(
        #version 450 core
        layout(location = 0) out vec4 color;
        
        in vec2 v_TexCoord;
        
        uniform sampler2D u_Texture;
        uniform vec4 u_Tint;

        void main() {
            color = texture(u_Texture, v_TexCoord) * u_Tint;
        }
    )";
    
    s_Data->TextureShader = std::make_unique<Graphics::Shader>(textureVertexSrc, textureFragmentSrc);
    s_Data->TextureShader->Bind();
    s_Data->TextureShader->SetInt("u_Texture", 0);

    // Circle Shader
    std::string circleVertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec2 a_Position;
        
        uniform mat4 u_ViewProjection;
        uniform mat4 u_Transform;
        
        out vec2 v_LocalPos;

        void main() {
            v_LocalPos = a_Position * 2.0;
            gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 0.0, 1.0);
        }
    )";

    std::string circleFragmentSrc = R"(
        #version 450 core
        layout(location = 0) out vec4 color;
        
        in vec2 v_LocalPos;
        
        uniform vec4 u_Color;
        uniform float u_Thickness;
        uniform float u_Fade;

        void main() {
            float distance = 1.0 - length(v_LocalPos);
            float circle = smoothstep(0.0, u_Fade, distance);
            circle *= smoothstep(u_Thickness + u_Fade, u_Thickness, distance);

            if (circle == 0.0) discard;

            color = u_Color;
            color.a *= circle;
        }
    )";
    
    s_Data->CircleShader = std::make_unique<Graphics::Shader>(circleVertexSrc, circleFragmentSrc);
}

void Renderer::Shutdown() {
    delete s_Data;
    s_Data = nullptr;
}

void Renderer::BeginScene(const float projection[16]) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    memcpy(s_Data->ViewProjection, projection, sizeof(float) * 16);
    s_Data->FlatColorShader->Bind();
    s_Data->FlatColorShader->SetMat4("u_ViewProjection", projection);
}

void Renderer::EndScene() {
    // Flush if batching
}

void Renderer::DrawQuad(const float position[2], const float size[2], const float color[4]) {
    s_Data->FlatColorShader->Bind();
    
    s_Data->FlatColorShader->SetFloat4("u_Color", color[0], color[1], color[2], color[3]);

    // Calculate Transform: Translate * Scale
    float translate[16], scale[16], transform[16];
    MatrixTranslate(translate, position);
    MatrixScale(scale, size);
    MatrixMultiply(transform, translate, scale);
    
    s_Data->FlatColorShader->SetMat4("u_Transform", transform);

    s_Data->QuadVertexArray->Bind();
    glDrawElements(GL_TRIANGLES, s_Data->QuadIndexBuffer->GetCount(), GL_UNSIGNED_INT, nullptr);
}

void Renderer::DrawQuad(const float position[2], const float size[2], const Graphics::Texture2D& texture, const float tintColor[4]) {
    s_Data->TextureShader->Bind();
    s_Data->TextureShader->SetMat4("u_ViewProjection", s_Data->ViewProjection);

    if (tintColor)
        s_Data->TextureShader->SetFloat4("u_Tint", tintColor[0], tintColor[1], tintColor[2], tintColor[3]);
    else
        s_Data->TextureShader->SetFloat4("u_Tint", 1.0f, 1.0f, 1.0f, 1.0f);

    texture.Bind(0);

    float translate[16], scale[16], transform[16];
    MatrixTranslate(translate, position);
    MatrixScale(scale, size);
    MatrixMultiply(transform, translate, scale);
    
    s_Data->TextureShader->SetMat4("u_Transform", transform);

    s_Data->QuadVertexArray->Bind();
    glDrawElements(GL_TRIANGLES, s_Data->QuadIndexBuffer->GetCount(), GL_UNSIGNED_INT, nullptr);
}

void Renderer::DrawCircle(const float position[2], float radius, const float color[4], float thickness, float fade) {
    s_Data->CircleShader->Bind();
    
    s_Data->CircleShader->SetFloat4("u_Color", color[0], color[1], color[2], color[3]);
    s_Data->CircleShader->SetFloat("u_Thickness", thickness);
    s_Data->CircleShader->SetFloat("u_Fade", fade);

    // Calculate Transform (scale by diameter = radius * 2)
    float scaleVec[2] = {radius * 2.0f, radius * 2.0f};
    float translate[16], scale[16], transform[16];
    MatrixTranslate(translate, position);
    MatrixScale(scale, scaleVec);
    MatrixMultiply(transform, translate, scale);
    
    s_Data->CircleShader->SetMat4("u_Transform", transform);
    s_Data->CircleShader->SetMat4("u_ViewProjection", s_Data->ViewProjection);

    s_Data->QuadVertexArray->Bind();
    glDrawElements(GL_TRIANGLES, s_Data->QuadIndexBuffer->GetCount(), GL_UNSIGNED_INT, nullptr);
}

} // namespace Perun
