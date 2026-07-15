#pragma once

#include "model/layout_state.h"

#include <filesystem>
#include <string>

namespace chunkmap {

class WorkspacePaths {
public:
    explicit WorkspacePaths(std::filesystem::path root);

    const std::filesystem::path& root() const { return root_; }
    std::filesystem::path output_root() const;
    std::filesystem::path project_root(const std::string& project_name) const;
    std::filesystem::path handoff_root(const std::string& project_name) const;

private:
    std::filesystem::path root_;
};

class ProjectPaths {
public:
    ProjectPaths() = default;
    ProjectPaths(std::filesystem::path workspace_root, std::string project_name);

    const std::filesystem::path& root() const { return project_root_; }
    const std::string& project_name() const { return project_name_; }

    std::filesystem::path project_json() const;
    std::filesystem::path global_prompt() const;
    std::filesystem::path concept_source() const;
    std::filesystem::path chunks_dir() const;
    std::filesystem::path prompts_dir() const;
    std::filesystem::path placements_json() const;
    std::filesystem::path seams_dir() const;
    std::filesystem::path seam_file(SeamKey key) const;
    std::filesystem::path chunk_prompt(ChunkCoord coord) const;
    std::filesystem::path chunk_image(ChunkCoord coord) const;
private:
    std::filesystem::path project_root_;
    std::string project_name_;
};

class WorkspaceHandoffPaths {
public:
    WorkspaceHandoffPaths(std::filesystem::path workspace_root, std::string project_name);
    std::filesystem::path root() const;
    std::filesystem::path prompt_authoring_guide() const;
    std::filesystem::path concept_dir() const;
    std::filesystem::path concept_regions_dir() const;
    std::filesystem::path concept_region(ChunkCoord coord) const;
    std::filesystem::path chunk_dir(ChunkCoord coord) const;
private:
    WorkspacePaths workspace_;
    std::string project_name_;
};

bool is_valid_project_name(const std::string& name);

}  // namespace chunkmap
