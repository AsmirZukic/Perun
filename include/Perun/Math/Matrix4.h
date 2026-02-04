#pragma once

#include <cstring>
#include <cmath>
#include "Vector2.h"

namespace Perun::Math {

struct Matrix4 {
    // Column-major order
    float elements[16];

    Matrix4() {
        std::memset(elements, 0, 16 * sizeof(float));
        elements[0] = 1.0f;
        elements[5] = 1.0f;
        elements[10] = 1.0f;
        elements[15] = 1.0f;
    }

    static Matrix4 Identity() {
        return Matrix4();
    }

    Matrix4 operator*(const Matrix4& other) const {
        Matrix4 result;
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                float sum = 0.0f;
                for (int e = 0; e < 4; e++) {
                    // Row from this, Col from other
                    // elements[col * 4 + row]
                    sum += elements[e * 4 + row] * other.elements[col * 4 + e];
                }
                result.elements[col * 4 + row] = sum;
            }
        }
        return result;
    }

    static Matrix4 Translate(const Vector2& translation) {
        Matrix4 result;
        // Col 3, Row 0 = x
        result.elements[12] = translation.x;
        // Col 3, Row 1 = y
        result.elements[13] = translation.y;
        return result;
    }
    
    static Matrix4 Scale(const Vector2& scale) {
        Matrix4 result;
        result.elements[0] = scale.x;
        result.elements[5] = scale.y;
        return result;
    }

    static Matrix4 Orthographic(float left, float right, float bottom, float top, float near, float far) {
        Matrix4 result;
        result.elements[0] = 2.0f / (right - left);
        result.elements[5] = 2.0f / (top - bottom);
        result.elements[10] = -2.0f / (far - near);
        
        result.elements[12] = -(right + left) / (right - left);
        result.elements[13] = -(top + bottom) / (top - bottom);
        result.elements[14] = -(far + near) / (far - near);
        return result;
    }
};

} // namespace Perun::Math
