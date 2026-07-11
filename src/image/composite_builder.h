#pragma once

#include "core/result.h"
#include "image/image_buffer.h"
#include "model/project_config.h"

#include <optional>
#include <vector>

namespace chunkmap {

class CompositeBuilder {
public:
    static Result<ImageBuffer> build(
        const ProjectConfig& config,
        const std::vector<std::optional<ImageBuffer>>& chunks);
};

}  // namespace chunkmap
