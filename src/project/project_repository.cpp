#include "project/project_repository.h"

#include "image/layout_renderer.h"
#include "io/atomic_file.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <algorithm>

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
        config.schema_version = 3;
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

Result<void> migrate_v2(json value, const ProjectPaths& paths) {
    value["schema_version"] = 3;
    return atomic_file::write_text(paths.project_json(), value.dump(2) + '\n');
}

Result<void> load_layout(Project& project) {
    std::error_code error;
    if (std::filesystem::is_regular_file(project.paths.placements_json(), error) && !error) {
        auto content = atomic_file::read_text(project.paths.placements_json());
        if (!content) return Result<void>::failure(content.error().code, content.error().message);
        try {
            const auto parsed = json::parse(content.value());
            for (const auto& item : parsed.at("placements")) {
                const ChunkCoord coord{item.at("x").get<int>(), item.at("y").get<int>()};
                const auto& offset = item.at("offset");
                if (!project.config.contains(coord) || !offset.is_array() || offset.size() != 2U) {
                    return Result<void>::failure(
                        "invalid_placements_json", "placements.json contains an invalid entry.");
                }
                ChunkPlacement placement{offset.at(0).get<int>(), offset.at(1).get<int>()};
                if (!placement.is_zero()) project.layout.placements[coord] = placement;
            }
        } catch (const std::exception& exception) {
            return Result<void>::failure(
                "invalid_placements_json", std::string("Invalid placements.json: ") + exception.what());
        }
    }

    error.clear();
    if (!std::filesystem::is_directory(project.paths.seams_dir(), error) || error) {
        return Result<void>::success();
    }
    for (const auto& entry : std::filesystem::directory_iterator(project.paths.seams_dir(), error)) {
        if (error) return Result<void>::failure("seam_read_failed", "Unable to read seams directory.");
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        auto content = atomic_file::read_text(entry.path());
        if (!content) return Result<void>::failure(content.error().code, content.error().message);
        try {
            const auto parsed = json::parse(content.value());
            const auto& first = parsed.at("first");
            SeamDefinition seam;
            seam.key.first = {first.at(0).get<int>(), first.at(1).get<int>()};
            const auto direction = parsed.at("direction").get<std::string>();
            if (direction != "right" && direction != "bottom") {
                return Result<void>::failure(
                    "invalid_seam_json", "Seam direction must be right or bottom.");
            }
            seam.key.direction = direction == "right"
                ? SeamDirection::Right : SeamDirection::Bottom;
            seam.feather_width = parsed.at("feather_width").get<int>();
            for (const auto& point : parsed.at("points")) {
                seam.points.push_back({
                    point.at("along").get<double>(), point.at("across").get<double>()});
            }
            auto valid = LayoutRenderer::validate_seam(project.config, seam);
            if (!valid) return valid;
            project.layout.seams[seam.key] = std::move(seam);
        } catch (const std::exception& exception) {
            return Result<void>::failure(
                "invalid_seam_json", std::string("Invalid Seam JSON: ") + exception.what());
        }
    }
    return Result<void>::success();
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
        } else if (parsed.at("schema_version").get<int>() == 2) {
            auto migrated = migrate_v2(parsed, paths);
            if (!migrated) return Result<Project>::failure(
                migrated.error().code, migrated.error().message);
            parsed["schema_version"] = 3;
        }
        auto config = decode_config(parsed, project_name);
        if (!config) return Result<Project>::failure(config.error().code, config.error().message);
        Project project{config.take_value(), std::move(paths), {}};
        auto layout = load_layout(project);
        if (!layout) return Result<Project>::failure(layout.error().code, layout.error().message);
        return Result<Project>::success(std::move(project));
    } catch (const std::exception& exception) {
        return Result<Project>::failure(
            "invalid_project_json", std::string("Unable to parse project.json: ") + exception.what());
    }
}

Result<void> ProjectRepository::save(const Project& project) const {
    const std::string content = encode_config(project.config).dump(2) + '\n';
    return atomic_file::write_text(project.paths.project_json(), content);
}

Result<void> ProjectRepository::save_placements(const Project& project) const {
    if (project.layout.placements.empty()) {
        std::error_code error;
        std::filesystem::remove(project.paths.placements_json(), error);
        if (error) return Result<void>::failure(
            "placement_remove_failed", "Unable to remove empty placements.json.");
        return Result<void>::success();
    }
    std::vector<std::pair<ChunkCoord, ChunkPlacement>> entries;
    entries.reserve(project.layout.placements.size());
    for (const auto& entry : project.layout.placements) entries.push_back(entry);
    std::sort(entries.begin(), entries.end(), [](const auto& first, const auto& second) {
        if (first.first.y != second.first.y) return first.first.y < second.first.y;
        return first.first.x < second.first.x;
    });
    json placements = json::array();
    for (const auto& entry : entries) {
        placements.push_back({
            {"x", entry.first.x}, {"y", entry.first.y},
            {"offset", {entry.second.offset_x, entry.second.offset_y}}});
    }
    return atomic_file::write_text(
        project.paths.placements_json(),
        json{{"schema_version", 1}, {"placements", placements}}.dump(2) + '\n');
}

Result<void> ProjectRepository::save_seam(const Project& project, SeamKey key) const {
    const auto found = project.layout.seams.find(key);
    if (found == project.layout.seams.end()) {
        return Result<void>::failure("seam_missing", "Seam override is missing.");
    }
    auto valid = LayoutRenderer::validate_seam(project.config, found->second);
    if (!valid) return valid;
    json points = json::array();
    for (const auto& point : found->second.points) {
        points.push_back({{"along", point.along}, {"across", point.across}});
    }
    const json content = {
        {"schema_version", 1},
        {"first", {key.first.x, key.first.y}},
        {"direction", seam_direction_name(key.direction)},
        {"feather_width", found->second.feather_width},
        {"points", points},
    };
    return atomic_file::write_text(project.paths.seam_file(key), content.dump(2) + '\n');
}

Result<void> ProjectRepository::remove_seam(const Project& project, SeamKey key) const {
    std::error_code error;
    std::filesystem::remove(project.paths.seam_file(key), error);
    if (error) return Result<void>::failure("seam_remove_failed", "Unable to remove Seam override.");
    return Result<void>::success();
}

}  // namespace chunkmap
