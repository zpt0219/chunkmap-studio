#pragma once

#include "model/chunk_coord.h"

#include <filesystem>
#include <string>

namespace chunkmap {

class WorkspacePaths {
public:
    explicit WorkspacePaths(std::filesystem::path root);

    const std::filesystem::path& root() const { return root_; }
    std::filesystem::path output_root() const;
    std::filesystem::path project_root(const std::string& project_name) const;

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
    std::filesystem::path concept_dir() const;
    std::filesystem::path concept_source() const;
    std::filesystem::path concept_regions_dir() const;
    std::filesystem::path concept_region(ChunkCoord coord) const;
    std::filesystem::path chunks_dir() const;
    std::filesystem::path chunk_dir(ChunkCoord coord) const;
    std::filesystem::path chunk_prompt(ChunkCoord coord) const;
    std::filesystem::path chunk_image(ChunkCoord coord) const;
    std::filesystem::path chunk_metadata(ChunkCoord coord) const;
    std::filesystem::path context_dir() const;
    std::filesystem::path concept_context_dir() const;
    std::filesystem::path chunk_context_dir(ChunkCoord coord) const;
    std::filesystem::path cache_dir() const;
    std::filesystem::path composite_png() const;
    std::filesystem::path seam_dir(ChunkCoord coord, const std::string& direction) const;
private:
    std::filesystem::path project_root_;
    std::string project_name_;
};

bool is_valid_project_name(const std::string& name);

}  // namespace chunkmap
