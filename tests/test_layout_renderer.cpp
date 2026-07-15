#include "image/layout_renderer.h"

#include <doctest/doctest.h>

namespace {

chunkmap::ImageBuffer solid(int width, int height,
                            std::uint8_t red, std::uint8_t blue) {
    chunkmap::ImageBuffer image(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            auto* pixel = image.pixel(x, y);
            pixel[0] = red;
            pixel[1] = 0U;
            pixel[2] = blue;
            pixel[3] = 255U;
        }
    }
    return image;
}

}  // namespace

TEST_CASE("placed chunk translation clamps edges without modifying its source") {
    chunkmap::ProjectConfig config;
    config.columns = 2;
    config.rows = 1;
    config.chunk_width = 6;
    config.chunk_height = 4;
    config.horizontal_overlap_ratio = 0.25;
    config.vertical_overlap_ratio = 0.25;
    chunkmap::ImageBuffer source(6, 4);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            auto* pixel = source.pixel(x, y);
            pixel[0] = static_cast<std::uint8_t>(x * 20);
            pixel[3] = 255U;
        }
    }
    const auto original = source.rgba();
    auto placed = chunkmap::LayoutRenderer::render_placed_chunk(
        source, config, {2, 0});
    REQUIRE(placed);
    CHECK(placed.value().pixel(0, 1)[0] == source.pixel(0, 1)[0]);
    CHECK(placed.value().pixel(3, 1)[0] == source.pixel(1, 1)[0]);
    CHECK(source.rgba() == original);
}

TEST_CASE("polyline seam chooses pixels inside only the overlap patch") {
    chunkmap::ProjectConfig config;
    config.columns = 2;
    config.rows = 1;
    config.chunk_width = 10;
    config.chunk_height = 10;
    config.horizontal_overlap_ratio = 0.4;
    config.vertical_overlap_ratio = 0.2;
    const auto first = solid(10, 10, 255U, 0U);
    const auto second = solid(10, 10, 0U, 255U);
    chunkmap::SeamDefinition seam;
    seam.key = {{0, 0}, chunkmap::SeamDirection::Right};
    seam.feather_width = 0;
    seam.points = {{0.0, 0.25}, {1.0, 0.75}};
    auto patch = chunkmap::LayoutRenderer::render_seam_patch(
        first, second, config, {}, {}, seam);
    REQUIRE(patch);
    CHECK(patch.value().width() == 4);
    CHECK(patch.value().height() == 10);
    CHECK(patch.value().pixel(0, 0)[0] == 255U);
    CHECK(patch.value().pixel(1, 0)[2] == 255U);
    CHECK(patch.value().pixel(1, 9)[0] == 255U);
    CHECK(patch.value().pixel(3, 9)[2] == 255U);
}
