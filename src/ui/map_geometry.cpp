#include "ui/map_geometry.h"

#include "image/image_pipeline.h"

#include <cstdint>
#include <limits>

namespace chunkmap {

Result<MapGeometry> map_geometry(const ProjectConfig& config) {
    auto image = image_geometry(config);
    if (!image) return Result<MapGeometry>::failure(image.error().code, image.error().message);
    MapGeometry result;
    result.chunk_width = *config.chunk_width;
    result.chunk_height = *config.chunk_height;
    result.step_x = image.value().step_x;
    result.step_y = image.value().step_y;
    const auto world_width = static_cast<std::int64_t>(result.chunk_width) +
        static_cast<std::int64_t>(config.columns - 1) * result.step_x;
    const auto world_height = static_cast<std::int64_t>(result.chunk_height) +
        static_cast<std::int64_t>(config.rows - 1) * result.step_y;
    if (world_width <= 0 || world_height <= 0 ||
        world_width > std::numeric_limits<int>::max() ||
        world_height > std::numeric_limits<int>::max()) {
        return Result<MapGeometry>::failure(
            "map_dimensions_overflow", "Map dimensions exceed the supported integer range.");
    }
    result.world_width = static_cast<int>(world_width);
    result.world_height = static_cast<int>(world_height);
    return Result<MapGeometry>::success(result);
}

std::optional<ChunkCoord> topmost_chunk_at(
    const ProjectConfig& config, double world_x, double world_y) {
    auto geometry = map_geometry(config);
    if (!geometry || world_x < 0.0 || world_y < 0.0 ||
        world_x >= geometry.value().world_width ||
        world_y >= geometry.value().world_height) {
        return std::nullopt;
    }
    // Chunks with larger coordinates are drawn later and own overlap hit regions.
    for (int y = config.rows - 1; y >= 0; --y) {
        for (int x = config.columns - 1; x >= 0; --x) {
            const double left = static_cast<double>(x * geometry.value().step_x);
            const double top = static_cast<double>(y * geometry.value().step_y);
            if (world_x >= left && world_x < left + geometry.value().chunk_width &&
                world_y >= top && world_y < top + geometry.value().chunk_height) {
                return ChunkCoord{x, y};
            }
        }
    }
    return std::nullopt;
}

}  // namespace chunkmap
