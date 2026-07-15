#pragma once

#include "model/layout_state.h"
#include "model/project_config.h"
#include "project/project_paths.h"

namespace chunkmap {

struct Project {
    ProjectConfig config;
    ProjectPaths paths;
    ProjectLayout layout;
};

}  // namespace chunkmap
