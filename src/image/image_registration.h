#pragma once

#include "core/result.h"
#include "image/image_pipeline.h"

namespace chunkmap {

struct ImageRegistrationResult {
    ImageBuffer image;
    int offset_x = 0;
    int offset_y = 0;
    double score_before = 0.0;
    double score_after = 0.0;
    bool applied = false;
};

class ImageRegistration {
public:
    static Result<ImageRegistrationResult> align(
        const ImageBuffer& source,
        const ProjectConfig& config,
        const NeighborImages& neighbors);
};

}  // namespace chunkmap
