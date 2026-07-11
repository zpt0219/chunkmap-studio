#include "image/composite_builder.h"

#include "image/image_pipeline.h"
#include "ui/map_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chunkmap {

namespace {

double incoming_weight(int position, int overlap, int feather) {
    const double center = static_cast<double>(overlap) / 2.0;
    if (feather <= 0) return position < center ? 0.0 : 1.0;
    const double start = center - static_cast<double>(feather) / 2.0;
    const double end = start + static_cast<double>(feather);
    if (position <= start) return 0.0;
    if (position >= end) return 1.0;
    return (static_cast<double>(position) - start) / (end - start);
}

void blend_pixel(std::uint8_t* destination,
                 const std::uint8_t* source,
                 double source_weight) {
    if (source[3] == 0) return;
    if (destination[3] == 0 || source_weight >= 1.0) {
        std::copy_n(source, 4, destination);
        return;
    }
    if (source_weight <= 0.0) return;
    for (int channel = 0; channel < 4; ++channel) {
        destination[channel] = static_cast<std::uint8_t>(std::lround(
            static_cast<double>(destination[channel]) * (1.0 - source_weight) +
            static_cast<double>(source[channel]) * source_weight));
    }
}

Result<void> validate_chunks(const ProjectConfig& config,
                             const std::vector<std::optional<ImageBuffer>>& chunks) {
    const auto expected = static_cast<std::size_t>(config.columns) *
                          static_cast<std::size_t>(config.rows);
    if (chunks.size() != expected) {
        return Result<void>::failure(
            "invalid_chunk_grid", "Composite input count does not match the project grid.");
    }
    for (const auto& chunk : chunks) {
        if (chunk && (chunk->width() != *config.chunk_width ||
                      chunk->height() != *config.chunk_height)) {
            return Result<void>::failure(
                "chunk_size_mismatch", "Composite input has an invalid chunk size.");
        }
    }
    return Result<void>::success();
}

}  // namespace

Result<ImageBuffer> CompositeBuilder::build(
    const ProjectConfig& config,
    const std::vector<std::optional<ImageBuffer>>& chunks) {
    auto geometry = image_geometry(config);
    if (!geometry) return Result<ImageBuffer>::failure(geometry.error().code, geometry.error().message);
    auto valid = validate_chunks(config, chunks);
    if (!valid) return Result<ImageBuffer>::failure(valid.error().code, valid.error().message);

    const int chunk_width = *config.chunk_width;
    const int chunk_height = *config.chunk_height;
    auto map = map_geometry(config);
    if (!map) return Result<ImageBuffer>::failure(map.error().code, map.error().message);
    const int world_width = map.value().world_width;
    const int world_height = map.value().world_height;

    std::vector<ImageBuffer> rows;
    rows.reserve(static_cast<std::size_t>(config.rows));
    for (int grid_y = 0; grid_y < config.rows; ++grid_y) {
        ImageBuffer row(world_width, chunk_height);
        for (int grid_x = 0; grid_x < config.columns; ++grid_x) {
            const auto& chunk = chunks[static_cast<std::size_t>(grid_y * config.columns + grid_x)];
            if (!chunk) continue;
            const bool blend_left = grid_x > 0 &&
                chunks[static_cast<std::size_t>(grid_y * config.columns + grid_x - 1)].has_value();
            const int destination_x = grid_x * geometry.value().step_x;
            for (int y = 0; y < chunk_height; ++y) {
                for (int x = 0; x < chunk_width; ++x) {
                    const double weight = blend_left && x < geometry.value().overlap_x
                        ? incoming_weight(x, geometry.value().overlap_x, geometry.value().feather_x)
                        : 1.0;
                    blend_pixel(row.pixel(destination_x + x, y), chunk->pixel(x, y), weight);
                }
            }
        }
        rows.push_back(std::move(row));
    }

    ImageBuffer result(world_width, world_height);
    for (int grid_y = 0; grid_y < config.rows; ++grid_y) {
        const int destination_y = grid_y * geometry.value().step_y;
        for (int y = 0; y < chunk_height; ++y) {
            const bool vertical_overlap = grid_y > 0 && y < geometry.value().overlap_y;
            const double weight = vertical_overlap
                ? incoming_weight(y, geometry.value().overlap_y, geometry.value().feather_y)
                : 1.0;
            for (int x = 0; x < world_width; ++x) {
                blend_pixel(result.pixel(x, destination_y + y), rows[grid_y].pixel(x, y), weight);
            }
        }
    }
    return Result<ImageBuffer>::success(std::move(result));
}

}  // namespace chunkmap
