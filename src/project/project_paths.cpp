#include "project/project_paths.h"

#include <cctype>

namespace chunkmap {

WorkspacePaths::WorkspacePaths(std::filesystem::path root)
    : root_(std::move(root)) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(root_, error);
    if (!error) root_ = std::move(absolute);
    root_ = root_.lexically_normal();
}

std::filesystem::path WorkspacePaths::output_root() const {
    return root_ / "output";
}

std::filesystem::path WorkspacePaths::project_root(const std::string& project_name) const {
    return output_root() / project_name;
}

ProjectPaths::ProjectPaths(std::filesystem::path workspace_root, std::string project_name)
    : project_root_(WorkspacePaths(std::move(workspace_root)).project_root(project_name)),
      project_name_(std::move(project_name)) {}

std::filesystem::path ProjectPaths::project_json() const { return project_root_ / "project.json"; }
std::filesystem::path ProjectPaths::concept_dir() const { return project_root_ / "concept"; }
std::filesystem::path ProjectPaths::concept_source() const { return concept_dir() / "source.png"; }
std::filesystem::path ProjectPaths::concept_regions_dir() const { return concept_dir() / "regions"; }
std::filesystem::path ProjectPaths::concept_region(ChunkCoord coord) const {
    return concept_regions_dir() / (coord_name(coord) + ".png");
}
std::filesystem::path ProjectPaths::chunks_dir() const { return project_root_ / "chunks"; }
std::filesystem::path ProjectPaths::chunk_dir(ChunkCoord coord) const {
    return chunks_dir() / coord_name(coord);
}
std::filesystem::path ProjectPaths::chunk_prompt(ChunkCoord coord) const {
    return chunk_dir(coord) / "prompt.md";
}
std::filesystem::path ProjectPaths::chunk_image(ChunkCoord coord) const {
    return chunk_dir(coord) / "image.png";
}
std::filesystem::path ProjectPaths::chunk_metadata(ChunkCoord coord) const {
    return chunk_dir(coord) / "metadata.json";
}
std::filesystem::path ProjectPaths::context_dir() const { return project_root_ / "context"; }
std::filesystem::path ProjectPaths::concept_context_dir() const { return context_dir() / "concept"; }
std::filesystem::path ProjectPaths::chunk_context_dir(ChunkCoord coord) const {
    return context_dir() / ("chunk_" + coord_name(coord));
}
std::filesystem::path ProjectPaths::cache_dir() const { return project_root_ / "cache"; }
std::filesystem::path ProjectPaths::composite_png() const { return cache_dir() / "composite.png"; }
std::filesystem::path ProjectPaths::seam_dir(ChunkCoord coord, const std::string& direction) const {
    return cache_dir() / "seams" / (coord_name(coord) + "_" + direction);
}
bool is_valid_project_name(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return false;
    for (unsigned char character : name) {
        if (!(std::isalnum(character) || character == '-' || character == '_')) return false;
    }
    return true;
}

}  // namespace chunkmap
