#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace chunkmap {

struct ChunkCoord {
    int x = 0;
    int y = 0;

    bool operator==(const ChunkCoord& other) const {
        return x == other.x && y == other.y;
    }

    bool operator!=(const ChunkCoord& other) const {
        return !(*this == other);
    }
};

struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord& coord) const {
        const auto x_hash = std::hash<int>{}(coord.x);
        const auto y_hash = std::hash<int>{}(coord.y);
        return x_hash ^ (y_hash + 0x9e3779b9U + (x_hash << 6U) + (x_hash >> 2U));
    }
};

inline std::string coord_name(const ChunkCoord& coord) {
    return std::to_string(coord.x) + "_" + std::to_string(coord.y);
}

}  // namespace chunkmap

