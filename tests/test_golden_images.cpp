#include "image/image_pipeline.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

using Color = std::array<std::uint8_t, 4>;

chunkmap::ImageBuffer solid(int width, int height, Color color) {
    chunkmap::ImageBuffer image(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::copy(color.begin(), color.end(), image.pixel(x, y));
        }
    }
    return image;
}

std::vector<std::string> read_golden(const std::string& name) {
    const auto path = std::filesystem::path(CHUNKMAP_TEST_SOURCE_DIR) / "golden" / name;
    std::ifstream input(path);
    REQUIRE_MESSAGE(input.good(), "Unable to open golden fixture: ", path.string());
    std::vector<std::string> rows;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.front() != '#') rows.push_back(line);
    }
    return rows;
}

char pixel_symbol(const std::uint8_t* pixel, const std::vector<std::pair<char, Color>>& palette) {
    if (pixel[3] == 0) return '.';
    for (const auto& entry : palette) {
        if (std::equal(entry.second.begin(), entry.second.end(), pixel)) return entry.first;
    }
    return '?';
}

void check_golden(const chunkmap::ImageBuffer& image,
                  const std::vector<std::string>& expected,
                  const std::vector<std::pair<char, Color>>& palette) {
    REQUIRE(static_cast<int>(expected.size()) == image.height());
    for (int y = 0; y < image.height(); ++y) {
        REQUIRE(static_cast<int>(expected[static_cast<std::size_t>(y)].size()) == image.width());
        for (int x = 0; x < image.width(); ++x) {
            CAPTURE(x);
            CAPTURE(y);
            CHECK(pixel_symbol(image.pixel(x, y), palette) ==
                  expected[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]);
        }
    }
}

chunkmap::ProjectConfig golden_config(int columns, int rows) {
    chunkmap::ProjectConfig config;
    config.name = "golden";
    config.columns = columns;
    config.rows = rows;
    config.chunk_width = 4;
    config.chunk_height = 4;
    config.horizontal_overlap_ratio = 0.25;
    config.vertical_overlap_ratio = 0.25;
    return config;
}

}  // namespace

TEST_CASE("four-side template matches golden pixels") {
    const Color red{220, 40, 40, 255};
    const Color green{40, 180, 70, 255};
    const Color blue{40, 80, 220, 255};
    const Color yellow{230, 190, 30, 255};
    chunkmap::NeighborImages neighbors;
    neighbors.top = solid(4, 4, red);
    neighbors.bottom = solid(4, 4, green);
    neighbors.left = solid(4, 4, blue);
    neighbors.right = solid(4, 4, yellow);
    auto result = chunkmap::TemplateBuilder::build(golden_config(3, 3), neighbors);
    REQUIRE(result.ok());
    check_golden(result.value(), read_golden("template_four_sides.txt"),
                 {{'R', red}, {'G', green}, {'B', blue}, {'Y', yellow}});
}
