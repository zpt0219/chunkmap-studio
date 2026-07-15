#include "image/image_registration.h"

#include <doctest/doctest.h>

#include <algorithm>

namespace {

chunkmap::ProjectConfig registration_config() {
    chunkmap::ProjectConfig config;
    config.name = "registration-test";
    config.columns = 3;
    config.rows = 3;
    config.chunk_width = 240;
    config.chunk_height = 160;
    config.horizontal_overlap_ratio = 0.4;
    config.vertical_overlap_ratio = 0.3;
    return config;
}

void set_pixel(chunkmap::ImageBuffer& image, int x, int y,
               int red, int green, int blue) {
    auto* pixel = image.pixel(x, y);
    pixel[0] = static_cast<std::uint8_t>(red);
    pixel[1] = static_cast<std::uint8_t>(green);
    pixel[2] = static_cast<std::uint8_t>(blue);
    pixel[3] = 255;
}

chunkmap::ImageBuffer patterned_image(int width, int height) {
    chunkmap::ImageBuffer image(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int cell = ((x / 7) + (y / 5) * 3) % 11;
            set_pixel(image, x, y,
                      (cell * 23 + x * 3) % 256,
                      (cell * 41 + y * 5) % 256,
                      (cell * 17 + x + y * 2) % 256);
        }
    }
    return image;
}

chunkmap::ImageBuffer shifted_left(const chunkmap::ImageBuffer& source, int amount) {
    chunkmap::ImageBuffer result(source.width(), source.height());
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            const int source_x = std::min(source.width() - 1, x + amount);
            std::copy_n(source.pixel(source_x, y), 4, result.pixel(x, y));
        }
    }
    return result;
}

chunkmap::ImageBuffer shifted_up(const chunkmap::ImageBuffer& source, int amount) {
    chunkmap::ImageBuffer result(source.width(), source.height());
    for (int y = 0; y < source.height(); ++y) {
        const int source_y = std::min(source.height() - 1, y + amount);
        for (int x = 0; x < source.width(); ++x) {
            std::copy_n(source.pixel(x, source_y), 4, result.pixel(x, y));
        }
    }
    return result;
}

chunkmap::NeighborImages right_neighbor(
    const chunkmap::ImageBuffer& target,
    const chunkmap::ProjectConfig& config) {
    chunkmap::NeighborImages neighbors;
    neighbors.right.emplace(*config.chunk_width, *config.chunk_height);
    auto geometry = chunkmap::image_geometry(config);
    REQUIRE(geometry);
    auto copied = neighbors.right->blit(
        target,
        {*config.chunk_width - geometry.value().overlap_x, 0,
         geometry.value().overlap_x, *config.chunk_height},
        {0, 0});
    REQUIRE(copied);
    return neighbors;
}

chunkmap::NeighborImages bottom_neighbor(
    const chunkmap::ImageBuffer& target,
    const chunkmap::ProjectConfig& config) {
    chunkmap::NeighborImages neighbors;
    neighbors.bottom.emplace(*config.chunk_width, *config.chunk_height);
    auto geometry = chunkmap::image_geometry(config);
    REQUIRE(geometry);
    for (int y = 0; y < *config.chunk_height; ++y) {
        for (int x = 0; x < *config.chunk_width; ++x) {
            std::copy_n(
                target.pixel(x, *config.chunk_height - 1), 4,
                neighbors.bottom->pixel(x, y));
        }
    }
    auto copied = neighbors.bottom->blit(
        target,
        {0, *config.chunk_height - geometry.value().overlap_y,
         *config.chunk_width, geometry.value().overlap_y},
        {0, 0});
    REQUIRE(copied);
    return neighbors;
}

}  // namespace

TEST_CASE("registration recovers a small horizontal translation") {
    const auto config = registration_config();
    const auto target = patterned_image(*config.chunk_width, *config.chunk_height);
    const auto source = shifted_left(target, 2);
    auto registered = chunkmap::ImageRegistration::align(
        source, config, right_neighbor(target, config));
    REQUIRE(registered);
    CHECK(registered.value().applied);
    CHECK(registered.value().offset_x >= 1);
    CHECK(registered.value().offset_x <= 2);
    CHECK(registered.value().offset_y == 0);
    CHECK(registered.value().score_after < registered.value().score_before);
    CHECK(registered.value().comparison.low_resolution.evaluated);
    CHECK(registered.value().comparison.projection.evaluated);
    CHECK(registered.value().comparison.low_resolution.offset_x >= 1);
    CHECK(registered.value().comparison.low_resolution.offset_x <= 2);
    CHECK(registered.value().comparison.projection.offset_x >= 1);
    CHECK(registered.value().comparison.projection.offset_x <= 2);
}

TEST_CASE("registration recovers a medium horizontal translation") {
    const auto config = registration_config();
    const auto target = patterned_image(*config.chunk_width, *config.chunk_height);
    const auto source = shifted_left(target, 40);
    auto registered = chunkmap::ImageRegistration::align(
        source, config, right_neighbor(target, config));
    REQUIRE(registered);
    CHECK(registered.value().applied);
    CHECK(registered.value().offset_x >= 38);
    CHECK(registered.value().offset_x <= 40);
    CHECK(registered.value().offset_y == 0);
    CHECK(registered.value().comparison.low_resolution.accepted);
    CHECK(registered.value().comparison.projection.accepted);
    CHECK(registered.value().comparison.low_resolution.offset_x >= 38);
    CHECK(registered.value().comparison.low_resolution.offset_x <= 40);
    CHECK(registered.value().comparison.projection.offset_x >= 38);
    CHECK(registered.value().comparison.projection.offset_x <= 40);
}

TEST_CASE("both registration algorithms recover vertical translation") {
    const auto config = registration_config();
    const auto target = patterned_image(*config.chunk_width, *config.chunk_height);
    const auto source = shifted_up(target, 11);
    auto registered = chunkmap::ImageRegistration::align(
        source, config, bottom_neighbor(target, config));
    REQUIRE(registered);
    CHECK(registered.value().applied);
    CHECK(registered.value().offset_y >= 10);
    CHECK(registered.value().offset_y <= 11);
    CHECK(registered.value().comparison.low_resolution.offset_y >= 10);
    CHECK(registered.value().comparison.low_resolution.offset_y <= 11);
    CHECK(registered.value().comparison.projection.offset_y >= 10);
    CHECK(registered.value().comparison.projection.offset_y <= 11);
}

TEST_CASE("registration leaves a flat overlap unchanged") {
    const auto config = registration_config();
    chunkmap::ImageBuffer source(*config.chunk_width, *config.chunk_height);
    chunkmap::ImageBuffer right(*config.chunk_width, *config.chunk_height);
    for (int y = 0; y < *config.chunk_height; ++y) {
        for (int x = 0; x < *config.chunk_width; ++x) {
            set_pixel(source, x, y, 80, 140, 90);
            set_pixel(right, x, y, 80, 140, 90);
        }
    }
    chunkmap::NeighborImages neighbors;
    neighbors.right = right;
    auto registered = chunkmap::ImageRegistration::align(source, config, neighbors);
    REQUIRE(registered);
    CHECK_FALSE(registered.value().applied);
    CHECK(registered.value().offset_x == 0);
    CHECK(registered.value().offset_y == 0);
    CHECK_FALSE(registered.value().comparison.low_resolution.accepted);
    CHECK_FALSE(registered.value().comparison.projection.accepted);
}

TEST_CASE("manual translation enforces the safe range") {
    const auto config = registration_config();
    const auto source = patterned_image(*config.chunk_width, *config.chunk_height);
    auto limits = chunkmap::ImageRegistration::limits(config);
    REQUIRE(limits);
    CHECK(chunkmap::ImageRegistration::translate(
        source, config, limits.value().maximum_x, 0));
    auto outside = chunkmap::ImageRegistration::translate(
        source, config, limits.value().maximum_x + 1, 0);
    CHECK_FALSE(outside);
    CHECK(outside.error().code == "registration_offset_out_of_range");
}
