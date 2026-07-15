#pragma once

#include "core/result.h"
#include "image/image_buffer.h"
#include "model/layout_state.h"
#include "model/project_config.h"

namespace chunkmap {

class LayoutRenderer {
public:
    static SeamDefinition default_seam(const ProjectConfig& config, SeamKey key);
    static Result<void> validate_seam(const ProjectConfig& config,
                                      const SeamDefinition& seam);
    static Result<ImageBuffer> render_placed_chunk(const ImageBuffer& source,
                                                   const ProjectConfig& config,
                                                   ChunkPlacement placement);
    static Result<ImageBuffer> render_seam_patch(const ImageBuffer& first,
                                                 const ImageBuffer& second,
                                                 const ProjectConfig& config,
                                                 ChunkPlacement first_placement,
                                                 ChunkPlacement second_placement,
                                                 const SeamDefinition& seam);
};

}  // namespace chunkmap
