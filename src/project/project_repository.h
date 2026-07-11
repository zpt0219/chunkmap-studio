#pragma once

#include "core/result.h"
#include "model/project.h"

#include <filesystem>
#include <string>

namespace chunkmap {

class ProjectRepository {
public:
    explicit ProjectRepository(std::filesystem::path workspace_root);

    const WorkspacePaths& workspace_paths() const { return workspace_paths_; }

    Result<Project> load(const std::string& project_name) const;
    Result<void> save(const Project& project) const;

private:
    WorkspacePaths workspace_paths_;
};

}  // namespace chunkmap

