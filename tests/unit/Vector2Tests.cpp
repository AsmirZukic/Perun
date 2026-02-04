#include <gtest/gtest.h>
#include "Perun/Math/Vector2.h"

using namespace Perun::Math;

TEST(Vector2Test, DefaultConstructor) {
    Vector2 v;
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
}

TEST(Vector2Test, ParamConstructor) {
    Vector2 v(1.5f, 2.5f);
    EXPECT_FLOAT_EQ(v.x, 1.5f);
    EXPECT_FLOAT_EQ(v.y, 2.5f);
}

TEST(Vector2Test, Addition) {
    Vector2 v1(1.0f, 2.0f);
    Vector2 v2(3.0f, 4.0f);
    Vector2 result = v1 + v2;
    EXPECT_EQ(result, Vector2(4.0f, 6.0f));
}
