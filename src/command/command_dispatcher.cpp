#include "command/command_dispatcher.h"

#include "image/image_buffer.h"
#include "io/atomic_file.h"
#include "project/project_service.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <filesystem>
#include <sstream>
#include <system_error>

namespace chunkmap {

namespace {

using nlohmann::json;

json config_json(const ProjectConfig& config) {
    json chunk_size = nullptr;
    if (config.has_chunk_size()) {
        chunk_size = json::array({*config.chunk_width, *config.chunk_height});
    }
    return {
        {"name", config.name},
        {"columns", config.columns},
        {"rows", config.rows},
        {"chunk_size", chunk_size},
        {"overlap_ratio", {config.horizontal_overlap_ratio, config.vertical_overlap_ratio}},
        {"feather_ratio", config.feather_ratio},
    };
}

template <typename T>
Result<const T*> require_payload(const CommandRequest& request) {
    const auto* payload = std::get_if<T>(&request.payload);
    if (!payload) {
        return Result<const T*>::failure(
            "invalid_command_payload", "Command payload does not match " + command_name(request.type) + ".");
    }
    return Result<const T*>::success(payload);
}

Result<std::string> require_project_name(const CommandRequest& request) {
    if (!request.project_name) {
        return Result<std::string>::failure(
            "missing_project", "This command requires --project <name>.");
    }
    return Result<std::string>::success(*request.project_name);
}

Result<Project> open_project(ProjectService& service, const CommandRequest& request) {
    auto name = require_project_name(request);
    if (!name) return Result<Project>::failure(name.error().code, name.error().message);
    return service.open_project(name.value());
}

void set_project_change(CommandResult& result,
                        const CommandRequest& request,
                        const std::string& project_name) {
    result.changes.project = make_project_key(request.workspace, project_name);
}

std::vector<ChunkCoord> prompt_coords(const std::filesystem::path& input) {
    std::vector<ChunkCoord> result;
    auto content = atomic_file::read_text(input);
    if (!content) return result;
    try {
        const auto parsed = json::parse(content.value());
        for (const auto& item : parsed.at("prompts")) {
            result.push_back({item.at("x").get<int>(), item.at("y").get<int>()});
        }
    } catch (...) {
        result.clear();
    }
    return result;
}

bool regular_file(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

}  // namespace

Result<CommandResult> CommandDispatcher::execute(const CommandRequest& request) const {
    try {
        ProjectService service(request.workspace);
        CommandResult result;

        if (request.type == CommandType::ProjectCreate) {
            auto payload = require_payload<ProjectCreatePayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            CreateProjectRequest create;
            create.name = payload.value()->name;
            create.concept_image = payload.value()->concept_image;
            create.columns = payload.value()->columns;
            create.rows = payload.value()->rows;
            create.horizontal_overlap_ratio = payload.value()->horizontal_overlap_ratio;
            create.vertical_overlap_ratio = payload.value()->vertical_overlap_ratio;
            create.feather_ratio = payload.value()->feather_ratio;
            auto project = service.create_project(create);
            if (!project) return Result<CommandResult>::failure(
                project.error().code, project.error().message);
            result.data = {
                {"project", config_json(project.value().config)},
                {"path", project.value().paths.root().string()},
            };
            result.text = "Created project " + create.name + " at " +
                          project.value().paths.root().string();
            result.project_snapshot = project.value();
            result.changes.project_changed = true;
            result.changes.concept_changed = true;
            set_project_change(result, request, create.name);
            return Result<CommandResult>::success(std::move(result));
        }

        auto project = open_project(service, request);
        if (!project) return Result<CommandResult>::failure(
            project.error().code, project.error().message);
        const std::string project_name = project.value().config.name;
        set_project_change(result, request, project_name);

        switch (request.type) {
        case CommandType::ProjectOpen:
            result.data = {{"project", config_json(project.value().config)},
                           {"path", project.value().paths.root().string()}};
            result.text = "Opened project " + project_name;
            result.project_snapshot = project.value();
            break;

        case CommandType::ProjectStatus: {
            auto status = service.status(project.value());
            if (!status) return Result<CommandResult>::failure(
                status.error().code, status.error().message);
            result.data = config_json(status.value().config);
            result.data["ready_chunks"] = status.value().ready_chunks;
            result.data["prompts_with_content"] = status.value().prompts_with_content;
            result.data["total_chunks"] = status.value().total_chunks;
            std::ostringstream text;
            text << "Project: " << project_name << '\n'
                 << "Grid: " << project.value().config.columns << 'x' << project.value().config.rows << '\n'
                 << "Ready chunks: " << status.value().ready_chunks << '/'
                 << status.value().total_chunks << '\n'
                 << "Prompts: " << status.value().prompts_with_content << '/'
                 << status.value().total_chunks;
            if (project.value().config.has_chunk_size()) {
                text << "\nChunk size: " << *project.value().config.chunk_width << 'x'
                     << *project.value().config.chunk_height;
            } else {
                text << "\nChunk size: Waiting for imported image";
            }
            result.text = text.str();
            result.project_snapshot = project.value();
            break;
        }

        case CommandType::ProjectValidate: {
            auto valid = service.validate(project.value());
            if (!valid) return Result<CommandResult>::failure(
                valid.error().code, valid.error().message);
            result.data = {{"valid", true}};
            result.text = "Project is valid.";
            break;
        }

        case CommandType::ConceptContext: {
            auto context = service.export_concept_context(project.value());
            if (!context) return Result<CommandResult>::failure(
                context.error().code, context.error().message);
            result.data = {
                {"concept_image", context.value().concept_image.string()},
                {"grid", {{"columns", project.value().config.columns},
                          {"rows", project.value().config.rows}}},
                {"regions_dir", context.value().regions_dir.string()},
                {"manifest", context.value().manifest.string()},
                {"output_schema", context.value().prompts_schema.string()},
                {"write_command", "chunkmap --project " + project_name +
                    " prompts import --input <prompts.json>"},
            };
            result.text = "Exported concept context to " +
                          context.value().manifest.parent_path().string();
            result.changes.changed_contexts.push_back(
                context.value().manifest.parent_path());
            break;
        }

        case CommandType::PromptsImport: {
            auto payload = require_payload<PathPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            auto imported = service.import_prompts(project.value(), payload.value()->path);
            if (!imported) return Result<CommandResult>::failure(
                imported.error().code, imported.error().message);
            result.data = {{"input", payload.value()->path.string()}};
            result.text = "Imported prompts from " + payload.value()->path.string();
            result.changes.changed_prompts = prompt_coords(payload.value()->path);
            break;
        }

        case CommandType::PromptShow: {
            auto payload = require_payload<CoordPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            auto prompt = service.read_prompt(project.value(), payload.value()->coord);
            if (!prompt) return Result<CommandResult>::failure(
                prompt.error().code, prompt.error().message);
            result.data = {{"coord", {payload.value()->coord.x, payload.value()->coord.y}},
                           {"prompt", prompt.value()}};
            result.text = prompt.value();
            break;
        }

        case CommandType::PromptSet: {
            auto payload = require_payload<PromptSetPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            auto written = service.write_prompt(
                project.value(), payload.value()->coord, payload.value()->text);
            if (!written) return Result<CommandResult>::failure(
                written.error().code, written.error().message);
            result.data = {{"coord", {payload.value()->coord.x, payload.value()->coord.y}},
                           {"path", project.value().paths.chunk_prompt(payload.value()->coord).string()},
                           {"prompt", payload.value()->text}};
            result.text = "Updated prompt " + coord_name(payload.value()->coord);
            result.changes.changed_prompts.push_back(payload.value()->coord);
            break;
        }

        case CommandType::ChunkContext: {
            auto payload = require_payload<CoordPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            auto context = service.export_chunk_context(project.value(), payload.value()->coord);
            if (!context) return Result<CommandResult>::failure(
                context.error().code, context.error().message);
            result.data = {
                {"chunk", {payload.value()->coord.x, payload.value()->coord.y}},
                {"expected_size", {*project.value().config.chunk_width,
                                   *project.value().config.chunk_height}},
                {"template", context.value().template_image.string()},
                {"mask", context.value().mask_image.string()},
                {"mask_convention", "white_generate_black_protect"},
                {"prompt", context.value().prompt.string()},
                {"manifest", context.value().manifest.string()},
                {"ready_neighbors", context.value().ready_directions},
                {"write_command", "chunkmap --project " + project_name +
                    " chunk write " + coord_name(payload.value()->coord) +
                    " --image <generated.png>"},
            };
            result.text = "Exported chunk context to " +
                          context.value().manifest.parent_path().string();
            result.changes.changed_contexts.push_back(
                context.value().manifest.parent_path());
            break;
        }

        case CommandType::ChunkImport:
        case CommandType::ChunkWrite: {
            auto payload = require_payload<ChunkImagePayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            const bool initialized_size = !project.value().config.has_chunk_size();
            auto written = request.type == CommandType::ChunkImport
                ? service.import_chunk_image(
                      project.value(), payload.value()->coord, payload.value()->image)
                : service.write_chunk_image(
                      project.value(), payload.value()->coord, payload.value()->image);
            if (!written) return Result<CommandResult>::failure(
                written.error().code, written.error().message);
            result.data = {
                {"chunk", {payload.value()->coord.x, payload.value()->coord.y}},
                {"image", written.value().image.string()},
                {"composite", written.value().composite.string()},
                {"normalization", {
                    {"added_left", written.value().added_left},
                    {"added_top", written.value().added_top},
                    {"added_right", written.value().added_right},
                    {"added_bottom", written.value().added_bottom}}},
                {"registration", {
                    {"applied", written.value().registration_applied},
                    {"offset", {written.value().registration_x,
                                written.value().registration_y}},
                    {"score_before", written.value().registration_score_before},
                    {"score_after", written.value().registration_score_after}}},
            };
            result.text = (request.type == CommandType::ChunkImport
                ? "Imported official chunk image " : "Wrote generated chunk image ") +
                written.value().image.string();
            result.changes.changed_chunks.push_back(payload.value()->coord);
            result.changes.composite_changed = true;
            if (initialized_size) {
                result.project_snapshot = project.value();
                result.changes.project_changed = true;
            }
            const auto coord = payload.value()->coord;
            for (const auto seam : std::vector<SeamKey>{
                     {coord, SeamDirection::Right},
                     {{coord.x - 1, coord.y}, SeamDirection::Right},
                     {coord, SeamDirection::Bottom},
                     {{coord.x, coord.y - 1}, SeamDirection::Bottom}}) {
                const auto second = seam.direction == SeamDirection::Right
                    ? ChunkCoord{seam.coord.x + 1, seam.coord.y}
                    : ChunkCoord{seam.coord.x, seam.coord.y + 1};
                if (project.value().config.contains(seam.coord) &&
                    project.value().config.contains(second) &&
                    regular_file(project.value().paths.chunk_image(seam.coord)) &&
                    regular_file(project.value().paths.chunk_image(second))) {
                    result.changes.changed_seams.push_back(seam);
                }
            }
            break;
        }

        case CommandType::ChunkShow: {
            auto payload = require_payload<CoordPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            if (!project.value().config.contains(payload.value()->coord)) {
                return Result<CommandResult>::failure(
                    "chunk_out_of_range", "Chunk coordinate is outside the project grid: " +
                        coord_name(payload.value()->coord));
            }
            const auto path = project.value().paths.chunk_image(payload.value()->coord);
            if (!regular_file(path)) {
                return Result<CommandResult>::failure("chunk_empty", "Chunk is Empty.");
            }
            result.data = {{"chunk", {payload.value()->coord.x, payload.value()->coord.y}},
                           {"image", path.string()}};
            result.text = path.string();
            break;
        }

        case CommandType::ChunkRemove: {
            auto payload = require_payload<CoordPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            auto removed = service.remove_chunk_image(project.value(), payload.value()->coord);
            if (!removed) return Result<CommandResult>::failure(
                removed.error().code, removed.error().message);
            result.data = {{"chunk", {payload.value()->coord.x, payload.value()->coord.y}}};
            result.text = "Removed chunk image.";
            result.changes.changed_chunks.push_back(payload.value()->coord);
            result.changes.composite_changed = true;
            break;
        }

        case CommandType::Render: {
            auto rendered = service.rebuild_composite(project.value());
            if (!rendered) return Result<CommandResult>::failure(
                rendered.error().code, rendered.error().message);
            result.data = {{"composite", rendered.value().string()}};
            result.text = rendered.value().string();
            result.changes.composite_changed = true;
            break;
        }

        case CommandType::SeamInspect: {
            auto payload = require_payload<SeamInspectPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            const auto direction = payload.value()->direction == CommandSeamDirection::Right
                ? SeamDirection::Right : SeamDirection::Bottom;
            auto analysis = service.inspect_seam(project.value(), payload.value()->coord, direction);
            if (!analysis) return Result<CommandResult>::failure(
                analysis.error().code, analysis.error().message);
            const std::string direction_name = direction == SeamDirection::Right ? "right" : "bottom";
            const auto directory = project.value().paths.seam_dir(
                payload.value()->coord, direction_name);
            result.data = {
                {"chunk", {payload.value()->coord.x, payload.value()->coord.y}},
                {"direction", direction_name},
                {"overlap_pixels", analysis.value().overlap_pixels},
                {"feather_pixels", analysis.value().feather_pixels},
                {"mean_absolute_rgb_difference", analysis.value().mean_absolute_rgb_difference},
                {"overlap_preview", (directory / "overlap.png").string()},
                {"difference_preview", (directory / "difference.png").string()},
            };
            result.text = "Seam MAE: " +
                          std::to_string(analysis.value().mean_absolute_rgb_difference);
            result.seam_analysis = analysis.take_value();
            result.changes.changed_seams.push_back({payload.value()->coord, direction});
            break;
        }

        case CommandType::MapExport: {
            auto payload = require_payload<PathPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            auto rendered = service.rebuild_composite(project.value());
            if (!rendered) return Result<CommandResult>::failure(
                rendered.error().code, rendered.error().message);
            auto image = ImageBuffer::load(rendered.value());
            if (!image) return Result<CommandResult>::failure(
                image.error().code, image.error().message);
            auto saved = image.value().save_png(payload.value()->path);
            if (!saved) return Result<CommandResult>::failure(
                saved.error().code, saved.error().message);
            std::error_code error;
            auto absolute = std::filesystem::absolute(payload.value()->path, error);
            if (error) absolute = payload.value()->path;
            result.data = {{"image", absolute.string()}};
            result.text = "Exported map to " + payload.value()->path.string();
            result.changes.composite_changed = true;
            break;
        }

        case CommandType::ProjectCreate:
            break;
        }
        return Result<CommandResult>::success(std::move(result));
    } catch (const std::exception& exception) {
        return Result<CommandResult>::failure("internal_error", exception.what());
    } catch (...) {
        return Result<CommandResult>::failure("internal_error", "Unknown command error.");
    }
}

}  // namespace chunkmap
