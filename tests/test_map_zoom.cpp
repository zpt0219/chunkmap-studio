#include "../desktop/src/map_zoom_state.h"

#include <doctest/doctest.h>

using chunkmap_desktop::MapZoomState;

TEST_CASE("map zoom uses discrete toolbar scales and preserves the mouse anchor") {
    MapZoomState view;
    view.fit(1000.0F, 800.0F, 1600.0F, 800.0F);
    CHECK(view.scale() == doctest::Approx(0.625F));

    const auto before = view.screen_to_world(300.0F, 200.0F);
    REQUIRE(view.step_at(1.0F, 300.0F, 200.0F));
    CHECK(view.scale() == doctest::Approx(1.0F));
    const auto after = view.screen_to_world(300.0F, 200.0F);
    CHECK(after.x == doctest::Approx(before.x));
    CHECK(after.y == doctest::Approx(before.y));

    REQUIRE(view.step_at(-1.0F, 300.0F, 200.0F));
    CHECK(view.scale() == doctest::Approx(0.5F));
}

TEST_CASE("map zoom reset and fit follow toolbar view semantics") {
    MapZoomState view;
    view.fit(1000.0F, 800.0F, 2000.0F, 1000.0F);
    CHECK(view.scale() == doctest::Approx(0.5F));
    CHECK(view.pan_x() == doctest::Approx(0.0F));
    CHECK(view.pan_y() == doctest::Approx(-300.0F));

    view.reset();
    CHECK(view.scale() == doctest::Approx(1.0F));
    CHECK(view.pan_x() == doctest::Approx(0.0F));
    CHECK(view.pan_y() == doctest::Approx(0.0F));
}
