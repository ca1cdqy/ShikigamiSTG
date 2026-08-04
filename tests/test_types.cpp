#include <shiki/core/types.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Vec2 basic operations", "[math]") {
    shiki::Vec2 v(1.0f, 2.0f);
    REQUIRE(v.x == 1.0f);
    REQUIRE(v.y == 2.0f);
}

TEST_CASE("Rect intersection", "[math]") {
    shiki::Rect a(0.0f, 0.0f, 10.0f, 10.0f);
    shiki::Rect b(5.0f, 5.0f, 10.0f, 10.0f);
    REQUIRE(a.intersects(b));
}

TEST_CASE("Clamp function", "[math]") {
    REQUIRE(shiki::clamp(5.0f, 0.0f, 10.0f) == 5.0f);
    REQUIRE(shiki::clamp(-1.0f, 0.0f, 10.0f) == 0.0f);
    REQUIRE(shiki::clamp(15.0f, 0.0f, 10.0f) == 10.0f);
}
