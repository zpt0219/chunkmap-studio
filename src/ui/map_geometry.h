#pragma once

#include "core/result.h"
#include "image/image_geometry.h"
#include "model/chunk_coord.h"
#include "model/project_config.h"

#include <optional>

namespace chunkmap {

struct MapGeometry {
    int world_width = 0;
    int world_height = 0;
    int chunk_width = 0;
    int chunk_height = 0;
    int step_x = 0;
    int step_y = 0;
};

Result<MapGeometry> map_geometry(const ProjectConfig& config);
std::optional<ChunkCoord> topmost_chunk_at(
    const ProjectConfig& config, double world_x, double world_y);

}  // namespace chunkmap
