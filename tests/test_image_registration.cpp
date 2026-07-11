#include "image/image_registration.h"

#include <doctest/doctest.h>

#include <algorithm>

namespace {

chunkmap::ProjectConfig registration_config() {
    chunkmap::ProjectConfig config;
    config.name = "registration-test";
    config.columns = 3;
    config.rows = 3;
    config.chunk_width = 128;
    config.chunk_height = 96;
    config.horizontal_overlap_ratio = 0.375;
    config.vertical_overlap_ratio = 0.25;
    config.feather_ratio = 0.05;
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

}  // namespace

TEST_CASE("registration recovers a small horizontal translation") {
    const auto config = registration_config();
    const auto target = patterned_image(*config.chunk_width, *config.chunk_height);
    const auto source = shifted_left(target, 2);
    chunkmap::ImageBuffer right(*config.chunk_width, *config.chunk_height);
    auto geometry = chunkmap::image_geometry(config);
    REQUIRE(geometry);
    auto copied = right.blit(
        target,
        {*config.chunk_width - geometry.value().overlap_x, 0,
         geometry.value().overlap_x, *config.chunk_height},
        {0, 0});
    REQUIRE(copied);
    chunkmap::NeighborImages neighbors;
    neighbors.right = right;

    auto registered = chunkmap::ImageRegistration::align(source, config, neighbors);
    REQUIRE(registered);
    CHECK(registered.value().applied);
    CHECK(registered.value().offset_x >= 1);
    CHECK(registered.value().offset_x <= 2);
    CHECK(registered.value().offset_y == 0);
    CHECK(registered.value().score_after < registered.value().score_before);
}

TEST_CASE("registration leaves an already matching flat overlap unchanged") {
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
}
