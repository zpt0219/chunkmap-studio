#pragma once

#include "model/project_config.h"
#include "project/project_paths.h"

namespace chunkmap {

struct Project {
    ProjectConfig config;
    ProjectPaths paths;
};

}  // namespace chunkmap

