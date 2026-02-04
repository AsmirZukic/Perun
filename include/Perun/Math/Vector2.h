#pragma once

#include <cmath>

namespace Perun::Math {

struct Vector2 {
    float x{0.0f};
    float y{0.0f};

    Vector2() = default;
    Vector2(float x, float y) : x(x), y(y) {}

    Vector2 operator+(const Vector2& other) const {
        return {x + other.x, y + other.y};
    }

    Vector2 operator-(const Vector2& other) const {
        return {x - other.x, y - other.y};
    }

    bool operator==(const Vector2& other) const {
        return std::abs(x - other.x) < 1e-5f && std::abs(y - other.y) < 1e-5f;
    }
};

} // namespace Perun::Math
