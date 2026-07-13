#include "project/project_service.h"

#include "image/image_buffer.h"
#include "image/image_pipeline.h"
#include "io/atomic_file.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <set>
#include <system_error>
#include <utility>

namespace chunkmap {

namespace {

using nlohmann::json;

bool valid_ratio(double value) {
    return std::isfinite(value) && value > 0.0 && value < 1.0;
}

bool path_is_regular_file(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

bool path_is_within(const std::filesystem::path& candidate,
                    const std::filesystem::path& root) {
    auto candidate_part = candidate.begin();
    for (auto root_part = root.begin(); root_part != root.end(); ++root_part, ++candidate_part) {
        if (candidate_part == candidate.end() || *candidate_part != *root_part) return false;
    }
    return true;
}

Result<std::filesystem::path> validate_concept_slice_output(
    const Project& project, const std::filesystem::path& requested, bool overwrite) {
    if (!requested.is_absolute()) {
        return Result<std::filesystem::path>::failure(
            "invalid_export_path", "Concept slice export requires an absolute output path.");
    }
    std::string extension = requested.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension != ".png") {
        return Result<std::filesystem::path>::failure(
            "invalid_export_path", "Concept slice export output must use the .png extension.");
    }
    std::error_code error;
    if (!std::filesystem::is_directory(requested.parent_path(), error) || error) {
        return Result<std::filesystem::path>::failure(
            "export_parent_missing", "Export parent directory does not exist.");
    }
    error.clear();
    if (std::filesystem::is_symlink(requested, error) && !error) {
        return Result<std::filesystem::path>::failure(
            "invalid_export_path", "Concept slice export cannot replace a symbolic link.");
    }
    error.clear();
    if (std::filesystem::is_directory(requested, error) && !error) {
        return Result<std::filesystem::path>::failure(
            "invalid_export_path", "Concept slice export output must be a file path.");
    }
    error.clear();
    if (std::filesystem::exists(requested, error) && !error && !overwrite) {
        return Result<std::filesystem::path>::failure(
            "export_exists", "Concept slice export output already exists.");
    }
    error.clear();
    const auto parent = std::filesystem::weakly_canonical(requested.parent_path(), error);
    if (error) {
        return Result<std::filesystem::path>::failure(
            "invalid_export_path", "Unable to normalize export output path.");
    }
    const auto output = (parent / requested.filename()).lexically_normal();
    const auto project_root = std::filesystem::weakly_canonical(project.paths.root(), error);
    if (error) {
        return Result<std::filesystem::path>::failure(
            "invalid_export_path", "Unable to normalize project path.");
    }
    if (path_is_within(output, project_root)) {
        return Result<std::filesystem::path>::failure(
            "export_inside_project", "Concept slice export must be outside the project directory.");
    }
    return Result<std::filesystem::path>::success(output);
}

Result<void> ensure_directory(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) {
        return Result<void>::failure(
            "create_directory_failed", "Unable to create directory: " + path.string());
    }
    return Result<void>::success();
}

std::string prompt_text(const json& item) {
    if (item.contains("prompt") && item["prompt"].is_string()) {
        return item["prompt"].get<std::string>();
    }
    return {};
}

std::string combined_prompt(std::string_view global, std::string_view chunk) {
    if (global.empty()) return std::string(chunk);
    if (chunk.empty()) return std::string(global);
    return "[GLOBAL VISUAL STYLE]\n" + std::string(global) +
           "\n\n[CHUNK CONTENT]\n" + std::string(chunk);
}

Result<NeighborImages> load_neighbors(const Project& project, ChunkCoord coord) {
    NeighborImages result;
    auto load = [&](ChunkCoord neighbor, std::optional<ImageBuffer>& destination) -> Result<void> {
        if (!project.config.contains(neighbor) ||
            !path_is_regular_file(project.paths.chunk_image(neighbor))) {
            return Result<void>::success();
        }
        auto image = ImageBuffer::load(project.paths.chunk_image(neighbor));
        if (!image) return Result<void>::failure(image.error().code, image.error().message);
        destination = image.take_value();
        return Result<void>::success();
    };
    for (const auto& entry : std::vector<std::pair<ChunkCoord, std::optional<ImageBuffer>*>>{
             {{coord.x, coord.y - 1}, &result.top},
             {{coord.x, coord.y + 1}, &result.bottom},
             {{coord.x - 1, coord.y}, &result.left},
             {{coord.x + 1, coord.y}, &result.right}}) {
        auto loaded = load(entry.first, *entry.second);
        if (!loaded) return Result<NeighborImages>::failure(loaded.error().code, loaded.error().message);
    }
    return Result<NeighborImages>::success(std::move(result));
}

}  // namespace

ProjectService::ProjectService(std::filesystem::path workspace_root)
    : repository_(std::move(workspace_root)) {}

Result<void> ProjectService::validate_config(const ProjectConfig& config) const {
    if (!is_valid_project_name(config.name)) {
        return Result<void>::failure("invalid_project_name", "Invalid project name: " + config.name);
    }
    if (config.schema_version != 2) {
        return Result<void>::failure("unsupported_schema", "Only project schema version 2 is supported.");
    }
    if (config.columns <= 0 || config.rows <= 0) {
        return Result<void>::failure("invalid_grid", "Columns and rows must be positive.");
    }
    constexpr std::int64_t kMaximumChunkCount = 1'000'000;
    const auto chunk_count = static_cast<std::int64_t>(config.columns) * config.rows;
    if (chunk_count > kMaximumChunkCount) {
        return Result<void>::failure(
            "grid_too_large", "Project grid cannot contain more than 1000000 chunks.");
    }
    if (!valid_ratio(config.horizontal_overlap_ratio) ||
        !valid_ratio(config.vertical_overlap_ratio)) {
        return Result<void>::failure("invalid_overlap_ratio", "Overlap ratios must be between 0 and 1.");
    }
    if (config.chunk_width.has_value() != config.chunk_height.has_value()) {
        return Result<void>::failure("invalid_chunk_size", "Chunk width and height must both be set or null.");
    }
    if (config.has_chunk_size() && (*config.chunk_width <= 0 || *config.chunk_height <= 0)) {
        return Result<void>::failure("invalid_chunk_size", "Chunk dimensions must be positive.");
    }
    return Result<void>::success();
}

Result<void> ProjectService::validate_coord(const Project& project, ChunkCoord coord) const {
    if (!project.config.contains(coord)) {
        return Result<void>::failure(
            "chunk_out_of_range",
            "Chunk coordinate is outside the project grid: " +
                std::to_string(coord.x) + "," + std::to_string(coord.y));
    }
    return Result<void>::success();
}

Result<Project> ProjectService::create_project(const CreateProjectRequest& request) {
    ProjectConfig config;
    config.name = request.name;
    config.columns = request.columns;
    config.rows = request.rows;
    config.horizontal_overlap_ratio = request.horizontal_overlap_ratio;
    config.vertical_overlap_ratio = request.vertical_overlap_ratio;

    auto config_result = validate_config(config);
    if (!config_result) {
        return Result<Project>::failure(config_result.error().code, config_result.error().message);
    }

    auto concept = ImageBuffer::load(request.concept_image);
    if (!concept) return Result<Project>::failure(concept.error().code, concept.error().message);

    ProjectPaths paths(repository_.workspace_paths().root(), request.name);
    std::error_code error;
    if (std::filesystem::exists(paths.root(), error)) {
        return Result<Project>::failure(
            "project_exists", "Project output already exists: " + paths.root().string());
    }

    const std::vector<std::filesystem::path> directories = {
        paths.chunks_dir(),
        paths.prompts_dir(),
    };
    for (const auto& directory : directories) {
        auto created = ensure_directory(directory);
        if (!created) {
            std::filesystem::remove_all(paths.root(), error);
            return Result<Project>::failure(created.error().code, created.error().message);
        }
    }

    auto concept_saved = concept.value().save_png(paths.concept_source());
    if (!concept_saved) {
        std::filesystem::remove_all(paths.root(), error);
        return Result<Project>::failure(concept_saved.error().code, concept_saved.error().message);
    }

    Project project{std::move(config), std::move(paths)};
    auto saved = repository_.save(project);
    if (!saved) {
        std::filesystem::remove_all(project.paths.root(), error);
        return Result<Project>::failure(saved.error().code, saved.error().message);
    }
    return Result<Project>::success(std::move(project));
}

Result<Project> ProjectService::open_project(const std::string& project_name) const {
    auto loaded = repository_.load(project_name);
    if (!loaded) return loaded;
    auto valid = validate_config(loaded.value().config);
    if (!valid) return Result<Project>::failure(valid.error().code, valid.error().message);
    return loaded;
}

Result<ProjectStatus> ProjectService::status(const Project& project) const {
    ProjectStatus result;
    result.config = project.config;
    result.total_chunks = project.config.columns * project.config.rows;

    for (int y = 0; y < project.config.rows; ++y) {
        for (int x = 0; x < project.config.columns; ++x) {
            const ChunkCoord coord{x, y};
            const auto image_path = project.paths.chunk_image(coord);
            if (path_is_regular_file(image_path)) {
                auto image = ImageBuffer::load(image_path);
                if (!image) {
                    return Result<ProjectStatus>::failure(image.error().code, image.error().message);
                }
                ++result.ready_chunks;
            }
            if (path_is_regular_file(project.paths.chunk_prompt(coord))) {
                auto prompt = atomic_file::read_text(project.paths.chunk_prompt(coord));
                if (!prompt) return Result<ProjectStatus>::failure(
                    prompt.error().code, prompt.error().message);
                if (!prompt.value().empty()) ++result.prompts_with_content;
            }
        }
    }
    return Result<ProjectStatus>::success(std::move(result));
}

Result<void> ProjectService::validate(const Project& project) const {
    auto config_result = validate_config(project.config);
    if (!config_result) return config_result;

    if (!path_is_regular_file(project.paths.concept_source())) {
        return Result<void>::failure("missing_concept", "Concept image is missing.");
    }
    for (int y = 0; y < project.config.rows; ++y) {
        for (int x = 0; x < project.config.columns; ++x) {
            const ChunkCoord coord{x, y};
            const auto image_path = project.paths.chunk_image(coord);
            if (path_is_regular_file(image_path)) {
                auto image = ImageBuffer::load(image_path);
                if (!image) return Result<void>::failure(image.error().code, image.error().message);
                if (!project.config.has_chunk_size() ||
                    image.value().width() != *project.config.chunk_width ||
                    image.value().height() != *project.config.chunk_height) {
                    return Result<void>::failure(
                        "chunk_size_mismatch", "Ready chunk has a different image size: " + coord_name(coord));
                }
            }
        }
    }
    return Result<void>::success();
}

Result<std::string> ProjectService::read_prompt(const Project& project, ChunkCoord coord) const {
    auto coord_result = validate_coord(project, coord);
    if (!coord_result) {
        return Result<std::string>::failure(coord_result.error().code, coord_result.error().message);
    }
    if (!path_is_regular_file(project.paths.chunk_prompt(coord))) {
        return Result<std::string>::success({});
    }
    return atomic_file::read_text(project.paths.chunk_prompt(coord));
}

Result<void> ProjectService::write_prompt(const Project& project,
                                          ChunkCoord coord,
                                          std::string_view text) const {
    auto coord_result = validate_coord(project, coord);
    if (!coord_result) return coord_result;
    if (text.empty()) {
        std::error_code error;
        std::filesystem::remove(project.paths.chunk_prompt(coord), error);
        return error ? Result<void>::failure("remove_failed", "Unable to clear Prompt file.")
                     : Result<void>::success();
    }
    return atomic_file::write_text(project.paths.chunk_prompt(coord), text);
}

Result<std::string> ProjectService::read_global_prompt(const Project& project) const {
    std::error_code error;
    if (!std::filesystem::exists(project.paths.global_prompt(), error) && !error) {
        return Result<std::string>::success({});
    }
    return atomic_file::read_text(project.paths.global_prompt());
}

Result<void> ProjectService::write_global_prompt(const Project& project,
                                                 std::string_view text) const {
    if (text.empty()) {
        std::error_code error;
        std::filesystem::remove(project.paths.global_prompt(), error);
        return error ? Result<void>::failure("remove_failed", "Unable to clear Global Prompt file.")
                     : Result<void>::success();
    }
    return atomic_file::write_text(project.paths.global_prompt(), text);
}

Result<void> ProjectService::import_prompts(const Project& project,
                                            const std::filesystem::path& json_path) const {
    auto content = atomic_file::read_text(json_path);
    if (!content) return Result<void>::failure(content.error().code, content.error().message);

    struct PromptEntry {
        ChunkCoord coord;
        std::string prompt;
    };
    std::vector<PromptEntry> entries;
    std::set<std::pair<int, int>> seen;

    try {
        const auto parsed = json::parse(content.value());
        if (!parsed.is_object() || !parsed.contains("prompts") || !parsed["prompts"].is_array()) {
            return Result<void>::failure(
                "invalid_prompts_json", "Prompt input must contain a prompts array.");
        }
        for (const auto& item : parsed["prompts"]) {
            if (!item.is_object() || !item.contains("x") || !item.contains("y")) {
                return Result<void>::failure(
                    "invalid_prompts_json", "Every prompt entry must contain x, y and prompt.");
            }
            const ChunkCoord coord{item.at("x").get<int>(), item.at("y").get<int>()};
            const auto prompt = prompt_text(item);
            if (!item.contains("prompt") || !item["prompt"].is_string()) {
                return Result<void>::failure(
                    "invalid_prompts_json", "Every prompt entry must contain a string prompt.");
            }
            auto coord_result = validate_coord(project, coord);
            if (!coord_result) return coord_result;
            if (!seen.emplace(coord.x, coord.y).second) {
                return Result<void>::failure(
                    "duplicate_prompt_coord", "Prompt input contains a duplicate coordinate.");
            }
            entries.push_back({coord, prompt});
        }
    } catch (const std::exception& exception) {
        return Result<void>::failure(
            "invalid_prompts_json", std::string("Unable to parse prompts JSON: ") + exception.what());
    }

    for (const auto& entry : entries) {
        auto written = write_prompt(project, entry.coord, entry.prompt);
        if (!written) return written;
    }
    return Result<void>::success();
}

Result<ConceptContext> ProjectService::export_concept_context(const Project& project) const {
    auto concept = ImageBuffer::load(project.paths.concept_source());
    if (!concept) return Result<ConceptContext>::failure(concept.error().code, concept.error().message);
    auto regions = ConceptSlicer::slice(concept.value(), project.config.columns, project.config.rows);
    if (!regions) return Result<ConceptContext>::failure(regions.error().code, regions.error().message);
    WorkspaceHandoffPaths handoff(repository_.workspace_paths().root(), project.config.name);
    std::error_code error;
    std::filesystem::remove_all(handoff.concept_dir(), error);
    auto created = ensure_directory(handoff.concept_regions_dir());
    if (!created) return Result<ConceptContext>::failure(created.error().code, created.error().message);

    ConceptContext context;
    context.concept_image = project.paths.concept_source();
    context.regions_dir = handoff.concept_regions_dir();
    context.manifest = handoff.concept_dir() / "manifest.json";
    context.prompts_schema = handoff.concept_dir() / "prompts.schema.json";
    json region_entries = json::array();
    for (int y = 0; y < project.config.rows; ++y) {
        for (int x = 0; x < project.config.columns; ++x) {
            const auto path = handoff.concept_region({x, y});
            const auto index = static_cast<std::size_t>(y * project.config.columns + x);
            auto saved = regions.value()[index].save_png(path);
            if (!saved) return Result<ConceptContext>::failure(
                saved.error().code, saved.error().message);
            context.regions.push_back(path);
            region_entries.push_back({{"coord", {x, y}}, {"image", path.string()}});
        }
    }
    const json manifest = {
        {"concept_image", context.concept_image.string()},
        {"grid", {{"columns", project.config.columns}, {"rows", project.config.rows}}},
        {"regions", region_entries},
        {"prompts_schema", context.prompts_schema.string()},
        {"write_command", "chunkmap --project " + project.config.name +
            " prompts import --input <prompts.json>"},
    };
    const json schema = {
        {"type", "object"},
        {"required", {"prompts"}},
        {"properties", {{"prompts", {
            {"type", "array"},
            {"items", {{"type", "object"}, {"required", {"x", "y", "prompt"}}}}
        }}}},
    };
    auto manifest_saved = atomic_file::write_text(context.manifest, manifest.dump(2) + '\n');
    if (!manifest_saved) return Result<ConceptContext>::failure(
        manifest_saved.error().code, manifest_saved.error().message);
    auto schema_saved = atomic_file::write_text(context.prompts_schema, schema.dump(2) + '\n');
    if (!schema_saved) return Result<ConceptContext>::failure(
        schema_saved.error().code, schema_saved.error().message);
    return Result<ConceptContext>::success(std::move(context));
}

Result<ConceptSliceExportResult> ProjectService::export_concept_slice(
    const Project& project,
    ChunkCoord coord,
    const std::filesystem::path& requested_output,
    bool overwrite) const {
    auto coord_result = validate_coord(project, coord);
    if (!coord_result) {
        return Result<ConceptSliceExportResult>::failure(
            coord_result.error().code, coord_result.error().message);
    }
    auto output = validate_concept_slice_output(project, requested_output, overwrite);
    if (!output) {
        return Result<ConceptSliceExportResult>::failure(
            output.error().code, output.error().message);
    }
    auto concept = ImageBuffer::load(project.paths.concept_source());
    if (!concept) {
        return Result<ConceptSliceExportResult>::failure(
            concept.error().code, concept.error().message);
    }
    auto region = ConceptSlicer::slice_one(
        concept.value(), project.config.columns, project.config.rows, coord);
    if (!region) {
        return Result<ConceptSliceExportResult>::failure(
            region.error().code, region.error().message);
    }
    auto saved = region.value().save_png(output.value());
    if (!saved) {
        return Result<ConceptSliceExportResult>::failure(
            saved.error().code, saved.error().message);
    }
    return Result<ConceptSliceExportResult>::success(
        {output.value(), region.value().width(), region.value().height()});
}

Result<ChunkContext> ProjectService::export_chunk_context(
    const Project& project, ChunkCoord coord) const {
    auto coord_result = validate_coord(project, coord);
    if (!coord_result) return Result<ChunkContext>::failure(
        coord_result.error().code, coord_result.error().message);
    auto neighbors = load_neighbors(project, coord);
    if (!neighbors) return Result<ChunkContext>::failure(neighbors.error().code, neighbors.error().message);
    auto prompt_text_result = read_prompt(project, coord);
    if (!prompt_text_result) return Result<ChunkContext>::failure(
        prompt_text_result.error().code, prompt_text_result.error().message);
    auto global_prompt_result = read_global_prompt(project);
    if (!global_prompt_result) return Result<ChunkContext>::failure(
        global_prompt_result.error().code, global_prompt_result.error().message);
    return export_chunk_context(project, coord, neighbors.value(),
                                global_prompt_result.value(), prompt_text_result.value());
}

Result<ChunkContext> ProjectService::export_chunk_context(
    const Project& project,
    ChunkCoord coord,
    const NeighborImages& neighbors,
    std::string_view global_prompt,
    std::string_view chunk_prompt) const {
    auto coord_result = validate_coord(project, coord);
    if (!coord_result) return Result<ChunkContext>::failure(
        coord_result.error().code, coord_result.error().message);
    auto geometry = image_geometry(project.config);
    if (!geometry) return Result<ChunkContext>::failure(geometry.error().code, geometry.error().message);
    auto template_image = TemplateBuilder::build(project.config, neighbors);
    if (!template_image) return Result<ChunkContext>::failure(
        template_image.error().code, template_image.error().message);

    std::error_code error;
    WorkspaceHandoffPaths handoff(repository_.workspace_paths().root(), project.config.name);
    const auto directory = handoff.chunk_dir(coord);
    std::filesystem::remove_all(directory, error);
    auto created = ensure_directory(directory);
    if (!created) return Result<ChunkContext>::failure(created.error().code, created.error().message);

    ChunkContext context;
    context.coord = coord;
    context.manifest = directory / "manifest.json";
    context.template_image = directory / "template.png";
    context.mask_image = directory / "mask.png";
    context.global_prompt = directory / "global_prompt.txt";
    context.chunk_prompt = directory / "chunk_prompt.txt";
    context.prompt = directory / "prompt.txt";
    json neighbor_entries = json::array();
    auto add_neighbor = [&](const char* direction, ChunkCoord neighbor, const auto& image) {
        if (!image) return;
        context.ready_directions.emplace_back(direction);
        neighbor_entries.push_back({
            {"direction", direction},
            {"coord", {neighbor.x, neighbor.y}},
            {"image", project.paths.chunk_image(neighbor).string()}});
    };
    add_neighbor("top", {coord.x, coord.y - 1}, neighbors.top);
    add_neighbor("bottom", {coord.x, coord.y + 1}, neighbors.bottom);
    add_neighbor("left", {coord.x - 1, coord.y}, neighbors.left);
    add_neighbor("right", {coord.x + 1, coord.y}, neighbors.right);

    const json manifest = {
        {"chunk", {coord.x, coord.y}},
        {"expected_size", {*project.config.chunk_width, *project.config.chunk_height}},
        {"overlap_pixels", {geometry.value().overlap_x, geometry.value().overlap_y}},
        {"template", context.template_image.string()},
        {"mask", context.mask_image.string()},
        {"mask_convention", "white_generate_black_protect"},
        {"global_prompt", context.global_prompt.string()},
        {"chunk_prompt", context.chunk_prompt.string()},
        {"prompt", context.prompt.string()},
        {"ready_neighbors", neighbor_entries},
        {"write_command", "chunkmap --project " + project.config.name + " chunk write " +
            coord_name(coord) + " --image <generated.png>"},
    };
    auto template_saved = template_image.value().save_png(context.template_image);
    if (!template_saved) return Result<ChunkContext>::failure(
        template_saved.error().code, template_saved.error().message);
    ImageBuffer mask(template_image.value().width(), template_image.value().height());
    for (int y = 0; y < mask.height(); ++y) {
        for (int x = 0; x < mask.width(); ++x) {
            const bool generate = template_image.value().pixel(x, y)[3] == 0;
            auto* pixel = mask.pixel(x, y);
            pixel[0] = generate ? 255U : 0U;
            pixel[1] = generate ? 255U : 0U;
            pixel[2] = generate ? 255U : 0U;
            pixel[3] = 255U;
        }
    }
    auto mask_saved = mask.save_png(context.mask_image);
    if (!mask_saved) return Result<ChunkContext>::failure(
        mask_saved.error().code, mask_saved.error().message);
    auto global_prompt_saved = atomic_file::write_text(
        context.global_prompt, global_prompt);
    if (!global_prompt_saved) return Result<ChunkContext>::failure(
        global_prompt_saved.error().code, global_prompt_saved.error().message);
    auto chunk_prompt_saved = atomic_file::write_text(
        context.chunk_prompt, chunk_prompt);
    if (!chunk_prompt_saved) return Result<ChunkContext>::failure(
        chunk_prompt_saved.error().code, chunk_prompt_saved.error().message);
    auto prompt_saved = atomic_file::write_text(
        context.prompt, combined_prompt(global_prompt, chunk_prompt));
    if (!prompt_saved) return Result<ChunkContext>::failure(
        prompt_saved.error().code, prompt_saved.error().message);
    auto manifest_saved = atomic_file::write_text(context.manifest, manifest.dump(2) + '\n');
    if (!manifest_saved) return Result<ChunkContext>::failure(
        manifest_saved.error().code, manifest_saved.error().message);
    return Result<ChunkContext>::success(std::move(context));
}

Result<ChunkWriteResult> ProjectService::import_chunk_image(
    Project& project, ChunkCoord coord, const std::filesystem::path& image_path) {
    return store_chunk_image(project, coord, image_path, true, false);
}

Result<ChunkWriteResult> ProjectService::import_chunk_image(
    Project& project, ChunkCoord coord, const std::filesystem::path& image_path,
    const NeighborImages& neighbors) {
    return store_chunk_image(project, coord, image_path, true, false, &neighbors);
}

Result<ChunkWriteResult> ProjectService::write_chunk_image(
    Project& project, ChunkCoord coord, const std::filesystem::path& image_path) {
    return store_chunk_image(project, coord, image_path, false, true);
}

Result<ChunkWriteResult> ProjectService::write_chunk_image(
    Project& project, ChunkCoord coord, const std::filesystem::path& image_path,
    const NeighborImages& neighbors) {
    return store_chunk_image(project, coord, image_path, false, true, &neighbors);
}

Result<ChunkWriteResult> ProjectService::store_chunk_image(
    Project& project,
    ChunkCoord coord,
    const std::filesystem::path& image_path,
    bool allow_size_initialization,
    bool require_ready_neighbor,
    const NeighborImages* supplied_neighbors) {
    auto coord_result = validate_coord(project, coord);
    if (!coord_result) return Result<ChunkWriteResult>::failure(
        coord_result.error().code, coord_result.error().message);
    auto source = ImageBuffer::load(image_path);
    if (!source) return Result<ChunkWriteResult>::failure(source.error().code, source.error().message);
    bool initialized_size = false;
    if (!project.config.has_chunk_size()) {
        if (!allow_size_initialization) {
            return Result<ChunkWriteResult>::failure(
                "missing_chunk_size", "Import a chunk image before writing generated chunks.");
        }
        if (source.value().width() < 2 || source.value().height() < 2) {
            return Result<ChunkWriteResult>::failure(
                "chunk_too_small", "Chunk dimensions must be at least 2x2.");
        }
        project.config.chunk_width = source.value().width();
        project.config.chunk_height = source.value().height();
        initialized_size = true;
    }
    NeighborImages loaded_neighbors;
    if (!supplied_neighbors) {
        auto neighbors = load_neighbors(project, coord);
        if (!neighbors) return Result<ChunkWriteResult>::failure(neighbors.error().code, neighbors.error().message);
        loaded_neighbors = neighbors.take_value();
        supplied_neighbors = &loaded_neighbors;
    }
    if (require_ready_neighbor && supplied_neighbors->count() == 0) {
        return Result<ChunkWriteResult>::failure(
            "no_ready_neighbor", "Generated chunk writes require at least one Ready orthogonal neighbor.");
    }
    auto normalized = ImageNormalizer::normalize(
        source.value(), project.config, coord, *supplied_neighbors);
    if (!normalized) return Result<ChunkWriteResult>::failure(
        normalized.error().code, normalized.error().message);
    ImageBuffer final_image = normalized.value().image;
    if (require_ready_neighbor) {
        auto protected_template = TemplateBuilder::build(project.config, *supplied_neighbors);
        if (!protected_template) return Result<ChunkWriteResult>::failure(
            protected_template.error().code, protected_template.error().message);
        for (int y = 0; y < final_image.height(); ++y) {
            for (int x = 0; x < final_image.width(); ++x) {
                if (protected_template.value().pixel(x, y)[3] != 0U) {
                    std::copy_n(protected_template.value().pixel(x, y), 4, final_image.pixel(x, y));
                }
            }
        }
    }
    auto saved = final_image.save_png(project.paths.chunk_image(coord));
    if (!saved) return Result<ChunkWriteResult>::failure(saved.error().code, saved.error().message);
    if (initialized_size) {
        auto project_saved = repository_.save(project);
        if (!project_saved) return Result<ChunkWriteResult>::failure(
            project_saved.error().code, project_saved.error().message);
    }
    ChunkWriteResult result;
    result.image = project.paths.chunk_image(coord);
    result.added_left = normalized.value().added_left;
    result.added_top = normalized.value().added_top;
    result.added_right = normalized.value().added_right;
    result.added_bottom = normalized.value().added_bottom;
    return Result<ChunkWriteResult>::success(std::move(result));
}

Result<void> ProjectService::remove_chunk_image(const Project& project, ChunkCoord coord) const {
    auto coord_result = validate_coord(project, coord);
    if (!coord_result) return coord_result;
    std::error_code error;
    std::filesystem::remove(project.paths.chunk_image(coord), error);
    if (error) return Result<void>::failure("remove_failed", "Unable to remove chunk image.");
    return Result<void>::success();
}

Result<SeamAnalysis> ProjectService::inspect_seam(
    const Project& project, ChunkCoord coord, SeamDirection direction) const {
    auto coord_result = validate_coord(project, coord);
    if (!coord_result) return Result<SeamAnalysis>::failure(
        coord_result.error().code, coord_result.error().message);
    const ChunkCoord second = direction == SeamDirection::Right
        ? ChunkCoord{coord.x + 1, coord.y} : ChunkCoord{coord.x, coord.y + 1};
    if (!project.config.contains(second)) {
        return Result<SeamAnalysis>::failure("seam_out_of_range", "Seam neighbor is outside the grid.");
    }
    auto first_image = ImageBuffer::load(project.paths.chunk_image(coord));
    auto second_image = ImageBuffer::load(project.paths.chunk_image(second));
    if (!first_image || !second_image) {
        return Result<SeamAnalysis>::failure(
            "seam_not_ready", "Both chunks must be Ready before inspecting their seam.");
    }
    auto analysis = SeamAnalyzer::analyze(
        first_image.value(), second_image.value(), project.config, direction);
    if (!analysis) return analysis;
    return analysis;
}

}  // namespace chunkmap
