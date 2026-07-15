#pragma once

#include "core/result.h"
#include "image/image_buffer.h"
#include "model/project.h"

#include <optional>
#include <filesystem>
#include <string>
#include <vector>

namespace chunkmap {

struct ChunkDocument {
    std::string prompt;
    bool ready = false;
    mutable bool image_loaded = false;
    mutable std::optional<ImageBuffer> image;
    mutable std::size_t last_image_access = 0;
    std::filesystem::file_time_type image_modified{};
};

class ProjectDocument {
public:
    static Result<ProjectDocument> load(Project project);

    Project& project() { return project_; }
    const Project& project() const { return project_; }
    ProjectConfig& config() { return project_.config; }
    const ProjectConfig& config() const { return project_.config; }
    std::string& global_prompt() { return global_prompt_; }
    const std::string& global_prompt() const { return global_prompt_; }

    ChunkDocument& chunk(ChunkCoord coord);
    const ChunkDocument& chunk(ChunkCoord coord) const;
    Result<const ImageBuffer*> image(ChunkCoord coord) const;
    ChunkPlacement placement(ChunkCoord coord) const;
    const SeamDefinition* seam_override(SeamKey key) const;
    void set_placement(ChunkCoord coord, ChunkPlacement placement);
    void set_seam(SeamDefinition seam);
    void reset_seam(SeamKey key);
    void replace_image(ChunkCoord coord, ImageBuffer image);
    void remove_image(ChunkCoord coord);
    void reset_empty_grid();
    int ready_count() const;
    int prompt_count() const;
    int cached_image_count() const;

private:
    std::size_t index(ChunkCoord coord) const;
    void touch_and_trim(ChunkCoord preserved) const;

    Project project_;
    std::string global_prompt_;
    std::vector<ChunkDocument> chunks_;
    mutable std::size_t access_clock_ = 0;
};

}  // namespace chunkmap
