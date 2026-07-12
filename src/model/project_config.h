#pragma once

#include <optional>
#include <string>

#include "model/chunk_coord.h"

namespace chunkmap {

struct ProjectConfig {
    int schema_version = 2;
    std::string name;
    int columns = 0;
    int rows = 0;
    std::optional<int> chunk_width;
    std::optional<int> chunk_height;
    double horizontal_overlap_ratio = 0.15;
    double vertical_overlap_ratio = 0.15;

    bool has_chunk_size() const {
        return chunk_width.has_value() && chunk_height.has_value();
    }

    bool contains(ChunkCoord coord) const {
        return coord.x >= 0 && coord.y >= 0 && coord.x < columns && coord.y < rows;
    }
};

}  // namespace chunkmap
