#pragma once

#include "image/image_buffer.h"
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
    bool global_prompt_changed = false;
    std::vector<ChunkCoord> changed_chunks;
    std::vector<ChunkCoord> changed_prompts;
};

struct CommandResult {
    nlohmann::json data = nlohmann::json::object();
    std::string text;
    ChangeSet changes;
    std::optional<Project> project_snapshot;
    std::shared_ptr<const ImageBuffer> changed_chunk_image;
    std::shared_ptr<const ImageBuffer> alignment_preview_image;
};

ProjectKey make_project_key(const std::filesystem::path& workspace,
                            const std::string& project_name);

}  // namespace chunkmap
