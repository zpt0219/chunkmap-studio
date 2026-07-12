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

struct NormalizedImage {
    ImageBuffer image;
    int added_left = 0;
    int added_top = 0;
    int added_right = 0;
    int added_bottom = 0;
};

Result<ImageGeometry> image_geometry(const ProjectConfig& config);

class ConceptSlicer {
public:
    static Result<std::vector<ImageBuffer>> slice(const ImageBuffer& concept,
                                                  int columns,
                                                  int rows);
};

class TemplateBuilder {
public:
    static Result<ImageBuffer> build(const ProjectConfig& config,
                                     const NeighborImages& neighbors);
};

class ImageNormalizer {
public:
    static Result<NormalizedImage> normalize(const ImageBuffer& source,
                                             const ProjectConfig& config,
                                             ChunkCoord coord,
                                             const NeighborImages& neighbors);
};

}  // namespace chunkmap
