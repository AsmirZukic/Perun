#include <gtest/gtest.h>
#include "Perun/Math/Matrix4.h"

using namespace Perun::Math;

TEST(Matrix4Test, Identity) {
    Matrix4 m = Matrix4::Identity();
    EXPECT_FLOAT_EQ(m.elements[0], 1.0f);
    EXPECT_FLOAT_EQ(m.elements[5], 1.0f);
}

TEST(Matrix4Test, MultiplyTranslation) {
    Matrix4 t = Matrix4::Translate(Vector2(5.0f, 0.0f));
    
    // Identity * Translation = Translation
    Matrix4 res = Matrix4::Identity() * t;
    EXPECT_FLOAT_EQ(res.elements[12], 5.0f);
    EXPECT_FLOAT_EQ(res.elements[13], 0.0f);
    
    // Translation * Translation = Double Translation
    Matrix4 res2 = t * t;
    EXPECT_FLOAT_EQ(res2.elements[12], 10.0f);
    EXPECT_FLOAT_EQ(res2.elements[13], 0.0f);
}

TEST(Matrix4Test, Scale) {
    Matrix4 s = Matrix4::Scale(Vector2(2.0f, 3.0f));
    EXPECT_FLOAT_EQ(s.elements[0], 2.0f);
    EXPECT_FLOAT_EQ(s.elements[5], 3.0f);
}
