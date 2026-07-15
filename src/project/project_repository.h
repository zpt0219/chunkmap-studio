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
    Result<void> save_placements(const Project& project) const;
    Result<void> save_seam(const Project& project, SeamKey key) const;
    Result<void> remove_seam(const Project& project, SeamKey key) const;

private:
    WorkspacePaths workspace_paths_;
};

}  // namespace chunkmap
