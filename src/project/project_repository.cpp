#include "project/project_repository.h"

#include "io/atomic_file.h"

#include <nlohmann/json.hpp>

#include <exception>

namespace chunkmap {

namespace {

using nlohmann::json;

json chunk_size_json(const ProjectConfig& config) {
    if (!config.has_chunk_size()) return nullptr;
    return json::array({*config.chunk_width, *config.chunk_height});
}

json encode_config(const ProjectConfig& config) {
    return json{
        {"schema_version", config.schema_version},
        {"name", config.name},
        {"columns", config.columns},
        {"rows", config.rows},
        {"concept_file", config.concept_file},
        {"chunk_size", chunk_size_json(config)},
        {"overlap_ratio", json::array({
            config.horizontal_overlap_ratio,
            config.vertical_overlap_ratio})},
        {"feather_ratio", config.feather_ratio},
    };
}

Result<ProjectConfig> decode_config(const json& value) {
    try {
        ProjectConfig config;
        config.schema_version = value.at("schema_version").get<int>();
        config.name = value.at("name").get<std::string>();
        config.columns = value.at("columns").get<int>();
        config.rows = value.at("rows").get<int>();
        config.concept_file = value.value("concept_file", "concept/source.png");
        config.feather_ratio = value.at("feather_ratio").get<double>();

        const auto& overlap = value.at("overlap_ratio");
        if (!overlap.is_array() || overlap.size() != 2U) {
            return Result<ProjectConfig>::failure(
                "invalid_project_json", "overlap_ratio must contain two values.");
        }
        config.horizontal_overlap_ratio = overlap[0].get<double>();
        config.vertical_overlap_ratio = overlap[1].get<double>();

        const auto& chunk_size = value.at("chunk_size");
        if (!chunk_size.is_null()) {
            if (!chunk_size.is_array() || chunk_size.size() != 2U) {
                return Result<ProjectConfig>::failure(
                    "invalid_project_json", "chunk_size must be null or contain two integers.");
            }
            config.chunk_width = chunk_size[0].get<int>();
            config.chunk_height = chunk_size[1].get<int>();
        }

        return Result<ProjectConfig>::success(std::move(config));
    } catch (const std::exception& exception) {
        return Result<ProjectConfig>::failure(
            "invalid_project_json", std::string("Invalid project.json: ") + exception.what());
    }
}

}  // namespace

ProjectRepository::ProjectRepository(std::filesystem::path workspace_root)
    : workspace_paths_(std::move(workspace_root)) {}

Result<Project> ProjectRepository::load(const std::string& project_name) const {
    if (!is_valid_project_name(project_name)) {
        return Result<Project>::failure("invalid_project_name", "Invalid project name: " + project_name);
    }

    ProjectPaths paths(workspace_paths_.root(), project_name);
    auto content = atomic_file::read_text(paths.project_json());
    if (!content) return Result<Project>::failure(content.error().code, content.error().message);

    try {
        auto parsed = nlohmann::json::parse(content.value());
        auto config = decode_config(parsed);
        if (!config) return Result<Project>::failure(config.error().code, config.error().message);
        if (config.value().name != project_name) {
            return Result<Project>::failure(
                "project_name_mismatch", "project.json name does not match its output directory.");
        }
        return Result<Project>::success(Project{config.take_value(), std::move(paths)});
    } catch (const std::exception& exception) {
        return Result<Project>::failure(
            "invalid_project_json", std::string("Unable to parse project.json: ") + exception.what());
    }
}

Result<void> ProjectRepository::save(const Project& project) const {
    const std::string content = encode_config(project.config).dump(2) + '\n';
    return atomic_file::write_text(project.paths.project_json(), content);
}

}  // namespace chunkmap
