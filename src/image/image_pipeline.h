#pragma once

#include "core/result.h"
#include "image/image_buffer.h"
#include "model/chunk_coord.h"
#include "model/project_config.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace chunkmap {

struct ImageGeometry {
    int overlap_x = 0;
    int overlap_y = 0;
    int step_x = 0;
    int step_y = 0;
};

struct NeighborImages {
    std::optional<ImageBuffer> top;
    std::optional<ImageBuffer> bottom;
    std::optional<ImageBuffer> left;
    std::optional<ImageBuffer> right;

    int count() const;
};

Result<ImageGeometry> image_geometry(const ProjectConfig& config);

class ConceptSlicer {
public:
    static Result<ImageBuffer> slice_one(const ImageBuffer& concept,
                                         int columns,
                                         int rows,
                                         ChunkCoord coord);
    static Result<std::vector<ImageBuffer>> slice(const ImageBuffer& concept,
                                                  int columns,
                                                  int rows);
};

class TemplateBuilder {
public:
    static Result<ImageBuffer> build(const ProjectConfig& config,
                                     const NeighborImages& neighbors);
};

}  // namespace chunkmap
