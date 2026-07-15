#pragma once

#include "model/chunk_coord.h"

#include <unordered_map>
#include <vector>

namespace chunkmap {

struct ChunkPlacement {
    int offset_x = 0;
    int offset_y = 0;

    bool operator==(const ChunkPlacement& other) const {
        return offset_x == other.offset_x && offset_y == other.offset_y;
    }

    bool is_zero() const { return offset_x == 0 && offset_y == 0; }
};

enum class SeamDirection {
    Right,
    Bottom,
};

struct SeamKey {
    ChunkCoord first;
    SeamDirection direction = SeamDirection::Right;

    bool operator==(const SeamKey& other) const {
        return first == other.first && direction == other.direction;
    }
};

struct SeamKeyHash {
    std::size_t operator()(const SeamKey& key) const {
        return ChunkCoordHash{}(key.first) ^
               (static_cast<std::size_t>(key.direction) + 0x9e3779b9U);
    }
};

struct SeamPoint {
    double along = 0.0;
    double across = 0.5;
};

struct SeamDefinition {
    SeamKey key;
    int feather_width = 0;
    std::vector<SeamPoint> points;
};

struct ProjectLayout {
    std::unordered_map<ChunkCoord, ChunkPlacement, ChunkCoordHash> placements;
    std::unordered_map<SeamKey, SeamDefinition, SeamKeyHash> seams;

    ChunkPlacement placement(ChunkCoord coord) const {
        const auto found = placements.find(coord);
        return found == placements.end() ? ChunkPlacement{} : found->second;
    }
};

inline ChunkCoord seam_second(const SeamKey& key) {
    return key.direction == SeamDirection::Right
        ? ChunkCoord{key.first.x + 1, key.first.y}
        : ChunkCoord{key.first.x, key.first.y + 1};
}

inline const char* seam_direction_name(SeamDirection direction) {
    return direction == SeamDirection::Right ? "right" : "bottom";
}

}  // namespace chunkmap
