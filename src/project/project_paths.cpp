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
std::filesystem::path WorkspacePaths::handoff_root(const std::string& project_name) const {
    return root_ / ".chunkmap" / "handoff" / project_name;
}

ProjectPaths::ProjectPaths(std::filesystem::path workspace_root, std::string project_name)
    : project_root_(WorkspacePaths(std::move(workspace_root)).project_root(project_name)),
      project_name_(std::move(project_name)) {}

std::filesystem::path ProjectPaths::project_json() const { return project_root_ / "project.json"; }
std::filesystem::path ProjectPaths::global_prompt() const { return project_root_ / "global_prompt.md"; }
std::filesystem::path ProjectPaths::concept_source() const { return project_root_ / "concept.png"; }
std::filesystem::path ProjectPaths::chunks_dir() const { return project_root_ / "chunks"; }
std::filesystem::path ProjectPaths::prompts_dir() const { return project_root_ / "prompts"; }
std::filesystem::path ProjectPaths::placements_json() const {
    return project_root_ / "placements.json";
}
std::filesystem::path ProjectPaths::seams_dir() const { return project_root_ / "seams"; }
std::filesystem::path ProjectPaths::seam_file(SeamKey key) const {
    return seams_dir() /
        (coord_name(key.first) + "_" + seam_direction_name(key.direction) + ".json");
}
std::filesystem::path ProjectPaths::chunk_prompt(ChunkCoord coord) const {
    return prompts_dir() / (coord_name(coord) + ".md");
}
std::filesystem::path ProjectPaths::chunk_image(ChunkCoord coord) const {
    return chunks_dir() / (coord_name(coord) + ".png");
}

WorkspaceHandoffPaths::WorkspaceHandoffPaths(
    std::filesystem::path workspace_root, std::string project_name)
    : workspace_(std::move(workspace_root)), project_name_(std::move(project_name)) {}
std::filesystem::path WorkspaceHandoffPaths::root() const {
    return workspace_.handoff_root(project_name_);
}
std::filesystem::path WorkspaceHandoffPaths::prompt_authoring_guide() const {
    return root() / "prompt-authoring-guide.md";
}
std::filesystem::path WorkspaceHandoffPaths::concept_dir() const { return root() / "concept"; }
std::filesystem::path WorkspaceHandoffPaths::concept_regions_dir() const {
    return concept_dir() / "regions";
}
std::filesystem::path WorkspaceHandoffPaths::concept_region(ChunkCoord coord) const {
    return concept_regions_dir() / (coord_name(coord) + ".png");
}
std::filesystem::path WorkspaceHandoffPaths::chunk_dir(ChunkCoord coord) const {
    return root() / ("chunk_" + coord_name(coord));
}
bool is_valid_project_name(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return false;
    for (unsigned char character : name) {
        if (!(std::isalnum(character) || character == '-' || character == '_')) return false;
    }
    return true;
}

}  // namespace chunkmap
