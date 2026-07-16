#include "command/command_dispatcher.h"

#include "image/image_buffer.h"
#include "image/full_map_exporter.h"
#include "image/layout_renderer.h"
#include "image/seam_analyzer.h"
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
    };
}

json registration_json(const ImageRegistrationResult& registration) {
    json result = {
        {"applied", registration.applied},
        {"offset", {registration.offset_x, registration.offset_y}},
        {"score_before", registration.score_before},
        {"score_after", registration.score_after},
        {"relative_improvement", registration.relative_improvement},
    };
    const auto candidate_json = [](const ImageRegistrationCandidate& candidate) {
        return json{
            {"method", registration_method_name(candidate.method)},
            {"offset", {candidate.offset_x, candidate.offset_y}},
            {"score", candidate.score},
            {"relative_improvement", candidate.relative_improvement},
            {"accepted", candidate.accepted},
        };
    };
    if (registration.comparison.low_resolution.evaluated &&
        registration.comparison.projection.evaluated) {
        result["comparison"] = {
            {"selected_method", registration_method_name(
                registration.comparison.selected_method)},
            {"candidates", {
                {"low_resolution_2d", candidate_json(
                    registration.comparison.low_resolution)},
                {"projection", candidate_json(
                    registration.comparison.projection)},
            }},
        };
    }
    return result;
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

Result<NeighborImages> document_neighbors(ProjectDocument& document, ChunkCoord coord) {
    NeighborImages neighbors;
    auto load = [&](ChunkCoord neighbor, std::optional<ImageBuffer>& destination) -> Result<void> {
        if (!document.config().contains(neighbor) || !document.chunk(neighbor).ready) {
            return Result<void>::success();
        }
        auto image = document.image(neighbor);
        if (!image) return Result<void>::failure(image.error().code, image.error().message);
        auto placed = LayoutRenderer::render_placed_chunk(
            *image.value(), document.config(), document.placement(neighbor));
        if (!placed) return Result<void>::failure(
            placed.error().code, placed.error().message);
        destination = placed.take_value();
        return Result<void>::success();
    };
    for (const auto& entry : std::vector<std::pair<ChunkCoord, std::optional<ImageBuffer>*>>{
             {{coord.x, coord.y - 1}, &neighbors.top},
             {{coord.x, coord.y + 1}, &neighbors.bottom},
             {{coord.x - 1, coord.y}, &neighbors.left},
             {{coord.x + 1, coord.y}, &neighbors.right}}) {
        auto loaded = load(entry.first, *entry.second);
        if (!loaded) return Result<NeighborImages>::failure(loaded.error().code, loaded.error().message);
    }
    return Result<NeighborImages>::success(std::move(neighbors));
}

}  // namespace

Result<CommandResult> CommandDispatcher::execute(
    const CommandRequest& request, const CommandProgressCallback& progress) {
    try {
        CommandResult result;

        if (request.type == CommandType::ProjectCurrent) {
            if (!active_project_) {
                return Result<CommandResult>::failure(
                    "no_project_open", "Desktop does not currently have a project open.");
            }
            result.data = {
                {"name", active_project_->project_name},
                {"workspace", active_project_->workspace.string()},
            };
            result.text = active_project_->project_name;
            result.changes.project = active_project_;
            return Result<CommandResult>::success(std::move(result));
        }

        ProjectService& service = session_.service(request.workspace);

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
            auto created = session_.create(request.workspace, create);
            if (!created) return Result<CommandResult>::failure(
                created.error().code, created.error().message);
            const Project& project = created.value()->project();
            result.data = {
                {"project", config_json(project.config)},
                {"path", project.paths.root().string()},
            };
            result.text = "Created project " + create.name + " at " +
                          project.paths.root().string();
            result.project_snapshot = project;
            result.changes.project_changed = true;
            set_project_change(result, request, create.name);
            active_project_ = result.changes.project;
            return Result<CommandResult>::success(std::move(result));
        }

        auto name = require_project_name(request);
        if (!name) return Result<CommandResult>::failure(name.error().code, name.error().message);
        auto opened = session_.open(
            request.workspace, name.value(), request.type == CommandType::ProjectOpen);
        if (!opened) return Result<CommandResult>::failure(
            opened.error().code, opened.error().message);
        ProjectDocument& document = *opened.value();
        Project& project = document.project();
        const std::string project_name = project.config.name;
        set_project_change(result, request, project_name);

        switch (request.type) {
        case CommandType::ProjectCurrent:
            return Result<CommandResult>::failure(
                "invalid_command_state", "Project current was not handled before project loading.");

        case CommandType::ProjectOpen:
            result.data = {{"project", config_json(project.config)},
                           {"path", project.paths.root().string()}};
            result.text = "Opened project " + project_name;
            result.project_snapshot = project;
            result.changes.project_changed = true;
            active_project_ = result.changes.project;
            break;

        case CommandType::ProjectGridSet: {
            auto payload = require_payload<ProjectGridSetPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            const bool grid_changed = payload.value()->columns != project.config.columns ||
                                      payload.value()->rows != project.config.rows;
            auto updated = service.update_grid(
                project, payload.value()->columns, payload.value()->rows);
            if (!updated) return Result<CommandResult>::failure(
                updated.error().code, updated.error().message);
            if (grid_changed) document.reset_empty_grid();
            result.data = {{"project", config_json(project.config)}};
            result.text = "Updated project grid to " +
                          std::to_string(project.config.columns) + "x" +
                          std::to_string(project.config.rows) + ".";
            result.project_snapshot = project;
            result.changes.project_changed = grid_changed;
            break;
        }

        case CommandType::ProjectStatus: {
            const int total_chunks = project.config.columns * project.config.rows;
            result.data = config_json(project.config);
            result.data["ready_chunks"] = document.ready_count();
            result.data["prompts_with_content"] = document.prompt_count();
            result.data["total_chunks"] = total_chunks;
            std::ostringstream text;
            text << "Project: " << project_name << '\n'
                 << "Grid: " << project.config.columns << 'x' << project.config.rows << '\n'
                 << "Ready chunks: " << document.ready_count() << '/' << total_chunks << '\n'
                 << "Prompts: " << document.prompt_count() << '/' << total_chunks;
            if (project.config.has_chunk_size()) {
                text << "\nChunk size: " << *project.config.chunk_width << 'x'
                     << *project.config.chunk_height;
            } else {
                text << "\nChunk size: Waiting for imported image";
            }
            result.text = text.str();
            result.project_snapshot = project;
            break;
        }

        case CommandType::ProjectValidate: {
            auto valid = service.validate(project);
            if (!valid) return Result<CommandResult>::failure(
                valid.error().code, valid.error().message);
            result.data = {{"valid", true}};
            result.text = "Project is valid.";
            break;
        }

        case CommandType::ConceptContext: {
            auto context = service.export_concept_context(project);
            if (!context) return Result<CommandResult>::failure(
                context.error().code, context.error().message);
            result.data = {
                {"concept_image", context.value().concept_image.string()},
                {"grid", {{"columns", project.config.columns},
                          {"rows", project.config.rows}}},
                {"regions_dir", context.value().regions_dir.string()},
                {"manifest", context.value().manifest.string()},
                {"authoring_guide", context.value().authoring_guide.string()},
                {"output_schema", context.value().prompts_schema.string()},
                {"write_command", "chunkmap --project " + project_name +
                    " prompts import --input <prompts.json>"},
            };
            result.text = "Exported concept context to " +
                          context.value().manifest.parent_path().string();
            break;
        }

        case CommandType::ConceptSliceExport: {
            auto payload = require_payload<ConceptSliceExportPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            auto exported = service.export_concept_slice(
                project, payload.value()->coord, payload.value()->output,
                payload.value()->overwrite);
            if (!exported) return Result<CommandResult>::failure(
                exported.error().code, exported.error().message);
            result.data = {
                {"coord", {payload.value()->coord.x, payload.value()->coord.y}},
                {"output", exported.value().output.string()},
                {"size", {exported.value().width, exported.value().height}},
            };
            result.text = "Exported concept slice to " + exported.value().output.string();
            result.changes = {};
            break;
        }

        case CommandType::PromptsImport: {
            auto payload = require_payload<PathPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            auto imported = service.import_prompts(project, payload.value()->path);
            if (!imported) return Result<CommandResult>::failure(
                imported.error().code, imported.error().message);
            result.data = {{"input", payload.value()->path.string()}};
            result.text = "Imported prompts from " + payload.value()->path.string();
            result.changes.changed_prompts = prompt_coords(payload.value()->path);
            for (const auto coord : result.changes.changed_prompts) {
                auto prompt = service.read_prompt(project, coord);
                if (prompt) document.chunk(coord).prompt = prompt.take_value();
            }
            break;
        }

        case CommandType::PromptShow: {
            auto payload = require_payload<CoordPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            if (!project.config.contains(payload.value()->coord)) {
                return Result<CommandResult>::failure("chunk_out_of_range", "Chunk coordinate is outside the project grid.");
            }
            const auto& prompt = document.chunk(payload.value()->coord).prompt;
            result.data = {{"coord", {payload.value()->coord.x, payload.value()->coord.y}},
                           {"prompt", prompt}};
            result.text = prompt;
            break;
        }

        case CommandType::PromptSet: {
            auto payload = require_payload<PromptSetPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            auto written = service.write_prompt(
                project, payload.value()->coord, payload.value()->text);
            if (!written) return Result<CommandResult>::failure(
                written.error().code, written.error().message);
            result.data = {{"coord", {payload.value()->coord.x, payload.value()->coord.y}},
                           {"path", project.paths.chunk_prompt(payload.value()->coord).string()},
                           {"prompt", payload.value()->text}};
            result.text = "Updated prompt " + coord_name(payload.value()->coord);
            document.chunk(payload.value()->coord).prompt = payload.value()->text;
            result.changes.changed_prompts.push_back(payload.value()->coord);
            break;
        }

        case CommandType::GlobalPromptShow: {
            result.data = {{"prompt", document.global_prompt()},
                           {"path", project.paths.global_prompt().string()}};
            result.text = document.global_prompt();
            break;
        }

        case CommandType::GlobalPromptSet: {
            auto payload = require_payload<GlobalPromptSetPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            auto written = service.write_global_prompt(project, payload.value()->text);
            if (!written) return Result<CommandResult>::failure(
                written.error().code, written.error().message);
            document.global_prompt() = payload.value()->text;
            result.data = {{"path", project.paths.global_prompt().string()},
                           {"prompt", payload.value()->text}};
            result.text = "Updated project Global Prompt";
            result.changes.global_prompt_changed = true;
            break;
        }

        case CommandType::ChunkContext: {
            auto payload = require_payload<CoordPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            if (!project.config.contains(payload.value()->coord)) {
                return Result<CommandResult>::failure(
                    "chunk_out_of_range", "Chunk coordinate is outside the project grid.");
            }
            auto neighbors = document_neighbors(document, payload.value()->coord);
            if (!neighbors) return Result<CommandResult>::failure(
                neighbors.error().code, neighbors.error().message);
            auto context = service.export_chunk_context(
                project, payload.value()->coord, neighbors.value(),
                document.global_prompt(), document.chunk(payload.value()->coord).prompt);
            if (!context) return Result<CommandResult>::failure(
                context.error().code, context.error().message);
            result.data = {
                {"chunk", {payload.value()->coord.x, payload.value()->coord.y}},
                {"expected_size", {*project.config.chunk_width,
                                   *project.config.chunk_height}},
                {"template", context.value().template_image.string()},
                {"mask", context.value().mask_image.string()},
                {"mask_convention", "white_generate_black_protect"},
                {"global_prompt", context.value().global_prompt.string()},
                {"chunk_prompt", context.value().chunk_prompt.string()},
                {"prompt", context.value().prompt.string()},
                {"manifest", context.value().manifest.string()},
                {"ready_neighbors", context.value().ready_directions},
                {"write_command", "chunkmap --project " + project_name +
                    " chunk write " + coord_name(payload.value()->coord) +
                    " --image <generated.png>"},
            };
            result.text = "Exported chunk context to " +
                          context.value().manifest.parent_path().string();
            break;
        }

        case CommandType::ChunkImport:
        case CommandType::ChunkWrite: {
            auto payload = require_payload<ChunkImagePayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            const bool initialized_size = !project.config.has_chunk_size();
            std::optional<std::filesystem::path> authoring_guide;
            if (request.type == CommandType::ChunkImport && initialized_size) {
                auto exported = service.export_prompt_authoring_guide(project);
                if (!exported) return Result<CommandResult>::failure(
                    exported.error().code, exported.error().message);
                authoring_guide = exported.take_value();
            }
            auto neighbors = document_neighbors(document, payload.value()->coord);
            if (!neighbors) return Result<CommandResult>::failure(
                neighbors.error().code, neighbors.error().message);
            auto written = request.type == CommandType::ChunkImport
                ? service.import_chunk_image(
                      project, payload.value()->coord, payload.value()->image, neighbors.value())
                : service.write_chunk_image(
                      project, payload.value()->coord, payload.value()->image, neighbors.value());
            if (!written) return Result<CommandResult>::failure(
                written.error().code, written.error().message);
            result.data = {
                {"chunk", {payload.value()->coord.x, payload.value()->coord.y}},
                {"image", written.value().image.string()},
                {"normalization", {
                    {"added_left", written.value().added_left},
                    {"added_top", written.value().added_top},
                    {"added_right", written.value().added_right},
                    {"added_bottom", written.value().added_bottom}}},
            };
            if (request.type == CommandType::ChunkWrite) {
                result.data["registration"] = registration_json(written.value().registration);
            }
            result.text = (request.type == CommandType::ChunkImport
                ? "Imported official chunk image " : "Wrote generated chunk image ") +
                written.value().image.string();
            if (request.type == CommandType::ChunkImport && initialized_size) {
                const std::string write_command =
                    "chunkmap --workspace \"" + request.workspace.string() +
                    "\" --project " + project_name +
                    " global-prompt set --file <global-prompt.md>";
                result.data["global_prompt_action"] = {
                    {"required", true},
                    {"reason", "first_chunk_imported"},
                    {"seed_image", written.value().image.string()},
                    {"authoring_guide", authoring_guide->string()},
                    {"instruction",
                     "Read the authoring guide completely, then analyze this formal chunk "
                     "image and write a project-wide Global Prompt. Follow the guide's "
                     "Global Prompt rules and do not describe this chunk's local layout."},
                    {"write_command", write_command},
                };
                result.text +=
                    "\nThis is the first formal chunk image. Analyze its visual style and "
                    "write the project Global Prompt with:\n" + write_command;
            }
            result.changes.changed_chunks.push_back(payload.value()->coord);
            if (request.type == CommandType::ChunkWrite) {
                result.project_snapshot = project;
                result.changes.project_changed = true;
            }
            auto stored_image = ImageBuffer::load(written.value().image);
            if (stored_image) {
                auto image = stored_image.take_value();
                result.changed_chunk_image = std::make_shared<ImageBuffer>(image);
                document.replace_image(payload.value()->coord, std::move(image));
            }
            if (initialized_size) {
                result.project_snapshot = project;
                result.changes.project_changed = true;
            }
            break;
        }

        case CommandType::ChunkAlignmentPreview: {
            auto payload = require_payload<ChunkAlignmentPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            const auto coord = payload.value()->coord;
            if (!project.config.contains(coord)) {
                return Result<CommandResult>::failure(
                    "chunk_out_of_range", "Chunk coordinate is outside the project grid.");
            }
            if (!document.chunk(coord).ready) {
                return Result<CommandResult>::failure(
                    "chunk_empty", "Chunk alignment requires a Ready chunk.");
            }
            auto source_image = document.image(coord);
            if (!source_image) return Result<CommandResult>::failure(
                source_image.error().code, source_image.error().message);
            ImageBuffer source = *source_image.value();
            auto neighbors = document_neighbors(document, coord);
            if (!neighbors) return Result<CommandResult>::failure(
                neighbors.error().code, neighbors.error().message);
            if (payload.value()->automatic && neighbors.value().count() == 0) {
                return Result<CommandResult>::failure(
                    "no_ready_neighbor", "Automatic alignment requires a Ready orthogonal neighbor.");
            }
            auto preview = service.preview_chunk_alignment(
                project, coord, source, neighbors.value(), payload.value()->automatic,
                payload.value()->offset_x, payload.value()->offset_y);
            if (!preview) return Result<CommandResult>::failure(
                preview.error().code, preview.error().message);
            auto limits = ImageRegistration::limits(project.config);
            if (!limits) return Result<CommandResult>::failure(
                limits.error().code, limits.error().message);
            result.data = {
                {"chunk", {coord.x, coord.y}},
                {"registration", registration_json(preview.value().registration)},
                {"limits", {limits.value().maximum_x, limits.value().maximum_y}},
            };
            result.text = "Previewed chunk alignment " + coord_name(coord) + ".";
            result.alignment_preview_image =
                std::make_shared<ImageBuffer>(std::move(preview.value().image));
            result.changes = {};
            break;
        }

        case CommandType::ChunkShiftApply: {
            auto payload = require_payload<ChunkAlignmentPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            if (payload.value()->automatic) {
                return Result<CommandResult>::failure(
                    "invalid_command_payload", "Chunk shift apply requires explicit offsets.");
            }
            const auto coord = payload.value()->coord;
            if (!project.config.contains(coord)) {
                return Result<CommandResult>::failure(
                    "chunk_out_of_range", "Chunk coordinate is outside the project grid.");
            }
            if (!document.chunk(coord).ready) {
                return Result<CommandResult>::failure(
                    "chunk_empty", "Chunk shift apply requires a Ready chunk.");
            }
            auto written = service.apply_chunk_shift(
                project, coord, payload.value()->offset_x, payload.value()->offset_y);
            if (!written) return Result<CommandResult>::failure(
                written.error().code, written.error().message);
            result.data = {
                {"chunk", {coord.x, coord.y}},
                {"image", written.value().image.string()},
                {"registration", registration_json(written.value().registration)},
            };
            result.text = "Applied chunk shift " + coord_name(coord) + ".";
            result.text = "Saved chunk placement " + coord_name(coord) + ".";
            result.changes.project_changed = true;
            result.project_snapshot = project;
            break;
        }

        case CommandType::ChunkShow: {
            auto payload = require_payload<CoordPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            if (!project.config.contains(payload.value()->coord)) {
                return Result<CommandResult>::failure(
                    "chunk_out_of_range", "Chunk coordinate is outside the project grid: " +
                        coord_name(payload.value()->coord));
            }
            const auto path = project.paths.chunk_image(payload.value()->coord);
            if (!document.chunk(payload.value()->coord).ready) {
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
            auto removed = service.remove_chunk_image(project, payload.value()->coord);
            if (!removed) return Result<CommandResult>::failure(
                removed.error().code, removed.error().message);
            result.data = {{"chunk", {payload.value()->coord.x, payload.value()->coord.y}}};
            result.text = "Removed chunk image.";
            document.remove_image(payload.value()->coord);
            result.changes.changed_chunks.push_back(payload.value()->coord);
            result.project_snapshot = project;
            result.changes.project_changed = true;
            break;
        }

        case CommandType::SeamInspect: {
            auto payload = require_payload<SeamInspectPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            const auto direction = payload.value()->direction == CommandSeamDirection::Right
                ? SeamDirection::Right : SeamDirection::Bottom;
            const ChunkCoord second = direction == SeamDirection::Right
                ? ChunkCoord{payload.value()->coord.x + 1, payload.value()->coord.y}
                : ChunkCoord{payload.value()->coord.x, payload.value()->coord.y + 1};
            if (!project.config.contains(payload.value()->coord) ||
                !project.config.contains(second)) {
                return Result<CommandResult>::failure(
                    "seam_out_of_range", "Seam neighbor is outside the project grid.");
            }
            auto first_image = document.image(payload.value()->coord);
            if (!first_image) return Result<CommandResult>::failure(
                first_image.error().code, first_image.error().message);
            auto second_image = document.image(second);
            if (!second_image) return Result<CommandResult>::failure(
                second_image.error().code, second_image.error().message);
            auto placed_first = LayoutRenderer::render_placed_chunk(
                *first_image.value(), project.config,
                document.placement(payload.value()->coord));
            auto placed_second = LayoutRenderer::render_placed_chunk(
                *second_image.value(), project.config, document.placement(second));
            if (!placed_first || !placed_second) {
                const auto& error = !placed_first ? placed_first.error() : placed_second.error();
                return Result<CommandResult>::failure(error.code, error.message);
            }
            auto analysis = SeamAnalyzer::analyze(
                placed_first.value(), placed_second.value(), project.config, direction);
            if (!analysis) return Result<CommandResult>::failure(
                analysis.error().code, analysis.error().message);
            const std::string direction_name = direction == SeamDirection::Right ? "right" : "bottom";
            result.data = {
                {"chunk", {payload.value()->coord.x, payload.value()->coord.y}},
                {"direction", direction_name},
                {"overlap_pixels", analysis.value().overlap_pixels},
                {"mean_absolute_rgb_difference", analysis.value().mean_absolute_rgb_difference},
            };
            result.text = "Seam MAE: " +
                          std::to_string(analysis.value().mean_absolute_rgb_difference);
            break;
        }

        case CommandType::SeamSet: {
            auto payload = require_payload<SeamSetPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            const SeamKey key = payload.value()->seam.key;
            if (!project.config.contains(key.first) ||
                !project.config.contains(seam_second(key))) {
                return Result<CommandResult>::failure(
                    "seam_out_of_range", "Seam pair is outside the project grid.");
            }
            if (!document.chunk(key.first).ready ||
                !document.chunk(seam_second(key)).ready) {
                return Result<CommandResult>::failure(
                    "seam_not_ready", "Both chunks must be Ready before editing their seam.");
            }
            auto saved = service.set_seam(project, payload.value()->seam);
            if (!saved) return Result<CommandResult>::failure(
                saved.error().code, saved.error().message);
            result.data = {{"first", {key.first.x, key.first.y}},
                           {"direction", seam_direction_name(key.direction)}};
            result.text = "Saved seam parameters.";
            result.changes.project_changed = true;
            result.project_snapshot = project;
            break;
        }

        case CommandType::SeamReset: {
            auto payload = require_payload<SeamResetPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            auto reset = service.reset_seam(project, payload.value()->key);
            if (!reset) return Result<CommandResult>::failure(
                reset.error().code, reset.error().message);
            result.text = "Reset seam to the automatic default.";
            result.changes.project_changed = true;
            result.project_snapshot = project;
            break;
        }

        case CommandType::MapExport: {
            auto payload = require_payload<MapExportPayload>(request);
            if (!payload) return Result<CommandResult>::failure(
                payload.error().code, payload.error().message);
            auto exported = export_full_map(
                document, {payload.value()->output, payload.value()->overwrite}, progress);
            if (!exported) return Result<CommandResult>::failure(
                exported.error().code, exported.error().message);
            result.data = {
                {"output", exported.value().output.string()},
                {"size", {exported.value().width, exported.value().height}},
                {"ready_chunks", exported.value().ready_chunks},
                {"empty_chunks", exported.value().empty_chunks},
            };
            result.text = "Exported full map to " + exported.value().output.string();
            result.changes = {};
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
