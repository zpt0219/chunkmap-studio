#pragma once

#include "image/seam_analyzer.h"
#include "model/project.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chunkmap {

struct ProjectKey {
    std::filesystem::path workspace;
    std::string project_name;

    bool operator==(const ProjectKey& other) const {
        return workspace == other.workspace && project_name == other.project_name;
    }
};

struct ChangeSet {
    std::optional<ProjectKey> project;
    bool project_changed = false;
    bool concept_changed = false;
    bool global_prompt_changed = false;
    std::vector<ChunkCoord> changed_chunks;
    std::vector<ChunkCoord> changed_placements;
    std::vector<SeamKey> changed_seams;
    std::vector<ChunkCoord> changed_prompts;
    std::vector<std::filesystem::path> changed_contexts;
};

struct CommandResult {
    nlohmann::json data = nlohmann::json::object();
    std::string text;
    ChangeSet changes;
    std::optional<Project> project_snapshot;
    std::optional<SeamAnalysis> seam_analysis;
    std::shared_ptr<const ImageBuffer> changed_chunk_image;
    std::shared_ptr<const ImageBuffer> alignment_preview_image;
};

ProjectKey make_project_key(const std::filesystem::path& workspace,
                            const std::string& project_name);

}  // namespace chunkmap
