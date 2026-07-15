#pragma once

#include "core/result.h"
#include "image/image_buffer.h"
#include "model/layout_state.h"
#include "model/project_config.h"

namespace chunkmap {

struct SeamAnalysis {
    SeamDirection direction = SeamDirection::Right;
    int overlap_pixels = 0;
    double mean_absolute_rgb_difference = 0.0;
    ImageBuffer overlap_preview;
    ImageBuffer difference_preview;
};

class SeamAnalyzer {
public:
    static Result<SeamAnalysis> analyze(const ImageBuffer& first,
                                        const ImageBuffer& second,
                                        const ProjectConfig& config,
                                        SeamDirection direction);
};

}  // namespace chunkmap
