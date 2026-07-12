#include "image/image_pipeline.h"
#include "image/seam_analyzer.h"
#include "ui/map_geometry.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>

namespace {

chunkmap::ImageBuffer solid(int width, int height, std::array<std::uint8_t, 4> color) {
    chunkmap::ImageBuffer image(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::copy(color.begin(), color.end(), image.pixel(x, y));
        }
    }
    return image;
}

chunkmap::ProjectConfig config() {
    chunkmap::ProjectConfig result;
    result.name = "pipeline";
    result.columns = 3;
    result.rows = 3;
    result.chunk_width = 10;
    result.chunk_height = 10;
    result.horizontal_overlap_ratio = 0.2;
    result.vertical_overlap_ratio = 0.2;
    return result;
}

void check_rgb(const chunkmap::ImageBuffer& image,
               int x,
               int y,
               std::array<std::uint8_t, 3> expected) {
    const auto* pixel = image.pixel(x, y);
    REQUIRE(pixel != nullptr);
    CHECK(pixel[0] == expected[0]);
    CHECK(pixel[1] == expected[1]);
    CHECK(pixel[2] == expected[2]);
    CHECK(pixel[3] == 255);
}

}  // namespace

TEST_CASE("concept slicing covers non-divisible source dimensions") {
    chunkmap::ImageBuffer source(7, 5);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            auto* pixel = source.pixel(x, y);
            pixel[0] = static_cast<std::uint8_t>(x);
            pixel[1] = static_cast<std::uint8_t>(y);
            pixel[3] = 255;
        }
    }
    auto sliced = chunkmap::ConceptSlicer::slice(source, 3, 2);
    REQUIRE(sliced.ok());
    REQUIRE(sliced.value().size() == 6);
    CHECK(sliced.value()[0].width() == 2);
    CHECK(sliced.value()[2].width() == 3);
    CHECK(sliced.value()[0].height() == 2);
    CHECK(sliced.value()[3].height() == 3);
    CHECK(sliced.value()[5].pixel(2, 2)[0] == 6);
    CHECK(sliced.value()[5].pixel(2, 2)[1] == 4);
}

TEST_CASE("template builder supports one and opposite two sides") {
    auto settings = config();
    chunkmap::NeighborImages one;
    one.top = solid(10, 10, {10, 20, 30, 255});
    auto top = chunkmap::TemplateBuilder::build(settings, one);
    REQUIRE(top.ok());
    check_rgb(top.value(), 5, 0, {10, 20, 30});
    CHECK(top.value().pixel(5, 2)[3] == 0);

    chunkmap::NeighborImages opposite;
    opposite.left = solid(10, 10, {100, 0, 0, 255});
    opposite.right = solid(10, 10, {0, 100, 0, 255});
    auto result = chunkmap::TemplateBuilder::build(settings, opposite);
    REQUIRE(result.ok());
    check_rgb(result.value(), 0, 5, {100, 0, 0});
    check_rgb(result.value(), 9, 5, {0, 100, 0});
    CHECK(result.value().pixel(5, 5)[3] == 0);
}

TEST_CASE("template builder supports adjacent three and four sides with fixed corner ownership") {
    auto settings = config();
    chunkmap::NeighborImages neighbors;
    neighbors.top = solid(10, 10, {10, 0, 0, 255});
    neighbors.bottom = solid(10, 10, {20, 0, 0, 255});
    neighbors.left = solid(10, 10, {30, 0, 0, 255});

    auto three = chunkmap::TemplateBuilder::build(settings, neighbors);
    REQUIRE(three.ok());
    check_rgb(three.value(), 0, 0, {30, 0, 0});
    check_rgb(three.value(), 5, 0, {10, 0, 0});
    check_rgb(three.value(), 0, 9, {30, 0, 0});
    check_rgb(three.value(), 5, 9, {20, 0, 0});

    neighbors.right = solid(10, 10, {40, 0, 0, 255});
    auto four = chunkmap::TemplateBuilder::build(settings, neighbors);
    REQUIRE(four.ok());
    check_rgb(four.value(), 0, 0, {30, 0, 0});
    check_rgb(four.value(), 9, 0, {40, 0, 0});
    check_rgb(four.value(), 0, 9, {30, 0, 0});
    check_rgb(four.value(), 9, 9, {40, 0, 0});
}

TEST_CASE("normalizer selects the 1px padding edge that best matches a neighbor") {
    auto settings = config();
    chunkmap::ImageBuffer source(9, 10);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 9; ++x) {
            auto* pixel = source.pixel(x, y);
            pixel[0] = static_cast<std::uint8_t>(x * 20);
            pixel[3] = 255;
        }
    }
    chunkmap::NeighborImages neighbors;
    neighbors.left = solid(10, 10, {0, 0, 0, 255});
    auto normalized = chunkmap::ImageNormalizer::normalize(
        source, settings, {1, 1}, neighbors);
    REQUIRE(normalized.ok());
    CHECK(normalized.value().added_left == 1);
    CHECK(normalized.value().added_right == 0);
    CHECK(normalized.value().image.width() == 10);
    CHECK(normalized.value().image.height() == 10);
}

TEST_CASE("normalizer rejects scaling and oversized images") {
    auto settings = config();
    chunkmap::NeighborImages neighbors;
    CHECK_FALSE(chunkmap::ImageNormalizer::normalize(
        solid(8, 10, {0, 0, 0, 255}), settings, {1, 1}, neighbors).ok());
    CHECK_FALSE(chunkmap::ImageNormalizer::normalize(
        solid(11, 10, {0, 0, 0, 255}), settings, {1, 1}, neighbors).ok());
}

TEST_CASE("seam analyzer reports RGB mean absolute error") {
    auto settings = config();
    auto first = solid(10, 10, {10, 20, 30, 255});
    auto second = solid(10, 10, {40, 50, 60, 255});
    auto result = chunkmap::SeamAnalyzer::analyze(
        first, second, settings, chunkmap::SeamDirection::Right);
    REQUIRE(result.ok());
    CHECK(result.value().overlap_pixels == 2);
    CHECK(result.value().mean_absolute_rgb_difference == doctest::Approx(30.0));
    CHECK(result.value().overlap_preview.width() == 2);
    CHECK(result.value().overlap_preview.height() == 10);
    CHECK(result.value().difference_preview.pixel(0, 0)[0] == 30);
}

TEST_CASE("map geometry gives visual topmost chunks ownership of overlaps") {
    auto settings = config();
    auto geometry = chunkmap::map_geometry(settings);
    REQUIRE(geometry.ok());
    CHECK(geometry.value().world_width == 26);
    CHECK(geometry.value().world_height == 26);
    CHECK(chunkmap::topmost_chunk_at(settings, 7.0, 7.0) == chunkmap::ChunkCoord{0, 0});
    CHECK(chunkmap::topmost_chunk_at(settings, 8.0, 5.0) == chunkmap::ChunkCoord{1, 0});
    CHECK(chunkmap::topmost_chunk_at(settings, 8.0, 8.0) == chunkmap::ChunkCoord{1, 1});
    CHECK_FALSE(chunkmap::topmost_chunk_at(settings, 26.0, 5.0).has_value());
}

TEST_CASE("map geometry rejects integer overflow") {
    auto settings = config();
    settings.columns = 1'000'000'000;
    settings.rows = 1;
    auto geometry = chunkmap::map_geometry(settings);
    REQUIRE_FALSE(geometry.ok());
    CHECK(geometry.error().code == "map_dimensions_overflow");
}
