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
        {"columns", config.columns},
        {"rows", config.rows},
        {"chunk_size", chunk_size_json(config)},
        {"overlap_ratio", json::array({
            config.horizontal_overlap_ratio,
            config.vertical_overlap_ratio})},
    };
}

Result<ProjectConfig> decode_config(const json& value, const std::string& project_name) {
    try {
        ProjectConfig config;
        config.schema_version = value.at("schema_version").get<int>();
        config.name = project_name;
        config.columns = value.at("columns").get<int>();
        config.rows = value.at("rows").get<int>();

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

Result<void> migrate_v1(const json& value, const ProjectPaths& paths) {
    try {
        const auto old_root = paths.root();
        auto copy = [](const std::filesystem::path& source,
                       const std::filesystem::path& destination) -> Result<void> {
            auto bytes = atomic_file::read_binary(source);
            if (!bytes) return Result<void>::failure(bytes.error().code, bytes.error().message);
            return atomic_file::write_binary(destination, bytes.value());
        };
        auto concept = copy(old_root / "concept" / "source.png", paths.concept_source());
        if (!concept) return concept;

        const int columns = value.at("columns").get<int>();
        const int rows = value.at("rows").get<int>();
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < columns; ++x) {
                const ChunkCoord coord{x, y};
                const auto old_dir = old_root / "chunks" / coord_name(coord);
                std::error_code error;
                if (std::filesystem::is_regular_file(old_dir / "image.png", error) && !error) {
                    auto image = copy(old_dir / "image.png", paths.chunk_image(coord));
                    if (!image) return image;
                }
                auto prompt = atomic_file::read_text(old_dir / "prompt.md");
                if (prompt && !prompt.value().empty()) {
                    auto written = atomic_file::write_text(paths.chunk_prompt(coord), prompt.value());
                    if (!written) return written;
                }
            }
        }

        auto global = atomic_file::read_text(paths.global_prompt());
        if (global && global.value().empty()) {
            std::error_code error;
            std::filesystem::remove(paths.global_prompt(), error);
        }

        ProjectConfig config;
        config.schema_version = 2;
        config.name = paths.project_name();
        config.columns = columns;
        config.rows = rows;
        const auto& overlap = value.at("overlap_ratio");
        config.horizontal_overlap_ratio = overlap.at(0).get<double>();
        config.vertical_overlap_ratio = overlap.at(1).get<double>();
        const auto& size = value.at("chunk_size");
        if (!size.is_null()) {
            config.chunk_width = size.at(0).get<int>();
            config.chunk_height = size.at(1).get<int>();
        }
        auto saved = atomic_file::write_text(paths.project_json(), encode_config(config).dump(2) + '\n');
        if (!saved) return saved;

        std::error_code error;
        std::filesystem::remove_all(old_root / "concept", error);
        std::filesystem::remove_all(old_root / "context", error);
        std::filesystem::remove_all(old_root / "cache", error);
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < columns; ++x) {
                std::filesystem::remove_all(
                    old_root / "chunks" / coord_name({x, y}), error);
            }
        }
        return Result<void>::success();
    } catch (const std::exception& exception) {
        return Result<void>::failure("migration_failed", exception.what());
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
        if (parsed.at("schema_version").get<int>() == 1) {
            auto migrated = migrate_v1(parsed, paths);
            if (!migrated) return Result<Project>::failure(
                migrated.error().code, migrated.error().message);
            auto migrated_content = atomic_file::read_text(paths.project_json());
            if (!migrated_content) return Result<Project>::failure(
                migrated_content.error().code, migrated_content.error().message);
            parsed = nlohmann::json::parse(migrated_content.value());
        }
        auto config = decode_config(parsed, project_name);
        if (!config) return Result<Project>::failure(config.error().code, config.error().message);
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
