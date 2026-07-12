#pragma once

#include "core/result.h"
#include "image/seam_analyzer.h"
#include "image/image_pipeline.h"
#include "model/project.h"
#include "project/project_repository.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace chunkmap {

struct CreateProjectRequest {
    std::string name;
    std::filesystem::path concept_image;
    int columns = 0;
    int rows = 0;
    double horizontal_overlap_ratio = 0.15;
    double vertical_overlap_ratio = 0.15;
};

struct ProjectStatus {
    ProjectConfig config;
    int ready_chunks = 0;
    int prompts_with_content = 0;
    int total_chunks = 0;
};

struct ConceptContext {
    std::filesystem::path manifest;
    std::filesystem::path concept_image;
    std::filesystem::path regions_dir;
    std::filesystem::path prompts_schema;
    std::vector<std::filesystem::path> regions;
};

struct ChunkContext {
    ChunkCoord coord;
    std::filesystem::path manifest;
    std::filesystem::path template_image;
    std::filesystem::path mask_image;
    std::filesystem::path global_prompt;
    std::filesystem::path chunk_prompt;
    std::filesystem::path prompt;
    std::vector<std::string> ready_directions;
};

struct ChunkWriteResult {
    std::filesystem::path image;
    int added_left = 0;
    int added_top = 0;
    int added_right = 0;
    int added_bottom = 0;
};

class ProjectService {
public:
    explicit ProjectService(std::filesystem::path workspace_root);

    Result<Project> create_project(const CreateProjectRequest& request);
    Result<Project> open_project(const std::string& project_name) const;
    Result<ProjectStatus> status(const Project& project) const;
    Result<void> validate(const Project& project) const;

    Result<ChunkWriteResult> import_chunk_image(
        Project& project, ChunkCoord coord, const std::filesystem::path& image_path);
    Result<ChunkWriteResult> import_chunk_image(
        Project& project, ChunkCoord coord, const std::filesystem::path& image_path,
        const NeighborImages& neighbors);
    Result<std::string> read_prompt(const Project& project, ChunkCoord coord) const;
    Result<void> write_prompt(const Project& project,
                              ChunkCoord coord,
                              std::string_view text) const;
    Result<void> import_prompts(const Project& project,
                                const std::filesystem::path& json_path) const;
    Result<std::string> read_global_prompt(const Project& project) const;
    Result<void> write_global_prompt(const Project& project, std::string_view text) const;
    Result<ConceptContext> export_concept_context(const Project& project) const;
    Result<ChunkContext> export_chunk_context(const Project& project, ChunkCoord coord) const;
    Result<ChunkContext> export_chunk_context(
        const Project& project, ChunkCoord coord, const NeighborImages& neighbors,
        std::string_view global_prompt, std::string_view chunk_prompt) const;
    Result<ChunkWriteResult> write_chunk_image(
        Project& project, ChunkCoord coord, const std::filesystem::path& image_path);
    Result<ChunkWriteResult> write_chunk_image(
        Project& project, ChunkCoord coord, const std::filesystem::path& image_path,
        const NeighborImages& neighbors);
    Result<void> remove_chunk_image(const Project& project, ChunkCoord coord) const;
    Result<SeamAnalysis> inspect_seam(
        const Project& project, ChunkCoord coord, SeamDirection direction) const;

private:
    Result<void> validate_config(const ProjectConfig& config) const;
    Result<void> validate_coord(const Project& project, ChunkCoord coord) const;
    Result<ChunkWriteResult> store_chunk_image(
        Project& project,
        ChunkCoord coord,
        const std::filesystem::path& image_path,
        bool allow_size_initialization,
        bool require_ready_neighbor,
        const NeighborImages* supplied_neighbors = nullptr);

    ProjectRepository repository_;
};

}  // namespace chunkmap
