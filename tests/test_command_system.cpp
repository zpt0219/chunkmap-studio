#include "command/command_codec.h"
#include "command/document_command_queue.h"
#include "io/atomic_file.h"
#include "ipc/desktop_ipc.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

namespace {

chunkmap::CommandRequest request(chunkmap::CommandType type,
                                 const std::filesystem::path& workspace,
                                 std::string id,
                                 std::optional<std::string> project = std::nullopt) {
    chunkmap::CommandRequest value;
    value.request_id = std::move(id);
    value.type = type;
    value.workspace = workspace;
    value.project_name = std::move(project);
    return value;
}

}  // namespace

TEST_CASE("command codec preserves typed payload") {
    auto original = request(chunkmap::CommandType::ChunkWrite, "/tmp/work space", "codec-1", "world");
    original.payload = chunkmap::ChunkImagePayload{{2, 3}, "/tmp/generated image.png"};
    auto decoded = chunkmap::decode_command_request(chunkmap::encode_command_request(original));
    REQUIRE(decoded);
    CHECK(decoded.value().request_id == "codec-1");
    CHECK(decoded.value().type == chunkmap::CommandType::ChunkWrite);
    CHECK(decoded.value().workspace == std::filesystem::path("/tmp/work space"));
    REQUIRE(decoded.value().project_name);
    CHECK(*decoded.value().project_name == "world");
    const auto* payload = std::get_if<chunkmap::ChunkImagePayload>(&decoded.value().payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->coord == chunkmap::ChunkCoord{2, 3});
    CHECK(payload->image == std::filesystem::path("/tmp/generated image.png"));
}

TEST_CASE("command codec preserves chunk alignment payload") {
    auto original = request(
        chunkmap::CommandType::ChunkAlignmentPreview,
        "/tmp/work space", "codec-alignment", "world");
    original.payload = chunkmap::ChunkAlignmentPayload{{2, 3}, -12, 7, true};
    auto decoded = chunkmap::decode_command_request(
        chunkmap::encode_command_request(original));
    REQUIRE(decoded);
    CHECK(decoded.value().type == chunkmap::CommandType::ChunkAlignmentPreview);
    const auto* payload =
        std::get_if<chunkmap::ChunkAlignmentPayload>(&decoded.value().payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->coord == chunkmap::ChunkCoord{2, 3});
    CHECK(payload->offset_x == -12);
    CHECK(payload->offset_y == 7);
    CHECK(payload->automatic);
}

TEST_CASE("command codec preserves editable seam payload") {
    auto original = request(
        chunkmap::CommandType::SeamSet, "/tmp/work space", "codec-seam", "world");
    chunkmap::SeamDefinition seam;
    seam.key = {{1, 2}, chunkmap::SeamDirection::Bottom};
    seam.feather_width = 18;
    seam.points = {{0.0, 0.4}, {0.6, 0.7}, {1.0, 0.5}};
    original.payload = chunkmap::SeamSetPayload{seam};
    auto decoded = chunkmap::decode_command_request(
        chunkmap::encode_command_request(original));
    REQUIRE(decoded);
    const auto* payload = std::get_if<chunkmap::SeamSetPayload>(&decoded.value().payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->seam.key == seam.key);
    CHECK(payload->seam.feather_width == 18);
    REQUIRE(payload->seam.points.size() == 3U);
    CHECK(payload->seam.points[1].along == doctest::Approx(0.6));
    CHECK(payload->seam.points[1].across == doctest::Approx(0.7));
}

TEST_CASE("command codec preserves project current without a project name") {
    auto original = request(
        chunkmap::CommandType::ProjectCurrent, "/tmp/work space", "codec-current");
    auto decoded = chunkmap::decode_command_request(
        chunkmap::encode_command_request(original));
    REQUIRE(decoded);
    CHECK(decoded.value().type == chunkmap::CommandType::ProjectCurrent);
    CHECK_FALSE(decoded.value().project_name.has_value());
    CHECK(std::holds_alternative<std::monostate>(decoded.value().payload));
}

TEST_CASE("command codec preserves global prompt payload") {
    auto original = request(
        chunkmap::CommandType::GlobalPromptSet, "/tmp/work space", "codec-global", "world");
    original.payload = chunkmap::GlobalPromptSetPayload{"Top-down pixel art"};
    auto decoded = chunkmap::decode_command_request(chunkmap::encode_command_request(original));
    REQUIRE(decoded);
    CHECK(decoded.value().type == chunkmap::CommandType::GlobalPromptSet);
    const auto* payload = std::get_if<chunkmap::GlobalPromptSetPayload>(&decoded.value().payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->text == "Top-down pixel art");
}

TEST_CASE("command codec preserves project grid payload") {
    auto original = request(
        chunkmap::CommandType::ProjectGridSet, "/tmp/work space", "codec-grid", "world");
    original.payload = chunkmap::ProjectGridSetPayload{5, 4};
    auto decoded = chunkmap::decode_command_request(chunkmap::encode_command_request(original));
    REQUIRE(decoded);
    CHECK(decoded.value().type == chunkmap::CommandType::ProjectGridSet);
    const auto* payload = std::get_if<chunkmap::ProjectGridSetPayload>(&decoded.value().payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->columns == 5);
    CHECK(payload->rows == 4);
}

TEST_CASE("command codec preserves full map export payload") {
    auto original = request(
        chunkmap::CommandType::MapExport, "/tmp/work space", "codec-export", "world");
    original.payload = chunkmap::MapExportPayload{"/tmp/full map.png", true};
    auto decoded = chunkmap::decode_command_request(chunkmap::encode_command_request(original));
    REQUIRE(decoded);
    CHECK(decoded.value().type == chunkmap::CommandType::MapExport);
    const auto* payload = std::get_if<chunkmap::MapExportPayload>(&decoded.value().payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->output == std::filesystem::path("/tmp/full map.png"));
    CHECK(payload->overwrite);
}

TEST_CASE("command codec preserves concept slice export payload") {
    auto original = request(
        chunkmap::CommandType::ConceptSliceExport,
        "/tmp/work space", "codec-concept-export", "world");
    original.payload = chunkmap::ConceptSliceExportPayload{
        {2, 3}, "/tmp/concept slice.png", true};
    auto decoded = chunkmap::decode_command_request(chunkmap::encode_command_request(original));
    REQUIRE(decoded);
    CHECK(decoded.value().type == chunkmap::CommandType::ConceptSliceExport);
    const auto* payload =
        std::get_if<chunkmap::ConceptSliceExportPayload>(&decoded.value().payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->coord == chunkmap::ChunkCoord{2, 3});
    CHECK(payload->output == std::filesystem::path("/tmp/concept slice.png"));
    CHECK(payload->overwrite);
}

TEST_CASE("IPC reply omits process-local change notifications") {
    auto original = request(
        chunkmap::CommandType::ProjectStatus, "/tmp/work space", "codec-reply", "world");
    chunkmap::CommandResult result;
    result.text = "Ready";
    result.changes.project_changed = true;
    result.changes.changed_chunks.push_back({1, 2});
    auto encoded = chunkmap::encode_ipc_reply(
        original, chunkmap::Result<chunkmap::CommandResult>::success(std::move(result)));

    CHECK(encoded.contains("protocol_version"));
    CHECK(encoded.contains("request_id"));
    CHECK(encoded.contains("result"));
    CHECK(encoded.contains("text"));
    CHECK_FALSE(encoded.contains("changes"));
    auto decoded = chunkmap::decode_ipc_reply(encoded);
    REQUIRE(decoded);
    CHECK(decoded.value().text == "Ready");
}

TEST_CASE("first chunk import requests global prompt exactly once") {
    const auto workspace = std::filesystem::temp_directory_path() /
        "chunkmap-global-prompt-action";
    std::error_code error;
    std::filesystem::remove_all(workspace, error);

    chunkmap::DocumentCommandQueue queue;
    auto create = request(chunkmap::CommandType::ProjectCreate, workspace, "global-1", "world");
    create.payload = chunkmap::ProjectCreatePayload{
        "world", std::filesystem::path(CHUNKMAP_TEST_SOURCE_DIR) / "fixtures/chunk.png",
        2, 2, 0.15, 0.15};
    REQUIRE(queue.submit(std::move(create)).get());

    auto first = request(chunkmap::CommandType::ChunkImport, workspace, "global-2", "world");
    first.payload = chunkmap::ChunkImagePayload{
        {0, 0}, std::filesystem::path(CHUNKMAP_TEST_SOURCE_DIR) / "fixtures/chunk.png"};
    auto first_result = queue.submit(std::move(first)).get();
    REQUIRE(first_result);
    REQUIRE(first_result.value().data.contains("global_prompt_action"));
    CHECK(first_result.value().data["global_prompt_action"]["required"] == true);
    CHECK(first_result.value().data["global_prompt_action"]["reason"] ==
          "first_chunk_imported");
    const auto guide = std::filesystem::path(
        first_result.value().data["global_prompt_action"]["authoring_guide"]
            .get<std::string>());
    CHECK(std::filesystem::is_regular_file(guide));

    auto second = request(chunkmap::CommandType::ChunkImport, workspace, "global-3", "world");
    second.payload = chunkmap::ChunkImagePayload{
        {1, 1}, std::filesystem::path(CHUNKMAP_TEST_SOURCE_DIR) / "fixtures/chunk.png"};
    auto second_result = queue.submit(std::move(second)).get();
    REQUIRE(second_result);
    CHECK_FALSE(second_result.value().data.contains("global_prompt_action"));

    queue.stop_and_drain();
    std::filesystem::remove_all(workspace, error);
}

TEST_CASE("chunk alignment preview is read only and save publishes placement only") {
    const auto workspace = std::filesystem::temp_directory_path() /
        "chunkmap-alignment-command";
    std::error_code error;
    std::filesystem::remove_all(workspace, error);
    const auto fixture = std::filesystem::path(CHUNKMAP_TEST_SOURCE_DIR) /
        "fixtures/chunk.png";

    chunkmap::DocumentCommandQueue queue;
    auto create = request(
        chunkmap::CommandType::ProjectCreate, workspace, "alignment-1", "world");
    create.payload = chunkmap::ProjectCreatePayload{
        "world", fixture, 2, 1, 0.15, 0.15};
    REQUIRE(queue.submit(std::move(create)).get());
    for (int x = 0; x < 2; ++x) {
        auto import = request(
            chunkmap::CommandType::ChunkImport, workspace,
            "alignment-import-" + std::to_string(x), "world");
        import.payload = chunkmap::ChunkImagePayload{{x, 0}, fixture};
        REQUIRE(queue.submit(std::move(import)).get());
    }

    auto auto_request = request(
        chunkmap::CommandType::ChunkAlignmentPreview,
        workspace, "alignment-auto", "world");
    auto_request.payload = chunkmap::ChunkAlignmentPayload{{1, 0}, 0, 0, true};
    auto automatic = queue.submit(std::move(auto_request)).get();
    REQUIRE(automatic);
    const auto& registration = automatic.value().data.at("registration");
    REQUIRE(registration.contains("comparison"));
    CHECK(registration.at("comparison").at("candidates").contains(
        "low_resolution_2d"));
    CHECK(registration.at("comparison").at("candidates").contains("projection"));

    auto preview_request = request(
        chunkmap::CommandType::ChunkAlignmentPreview,
        workspace, "alignment-preview", "world");
    preview_request.payload = chunkmap::ChunkAlignmentPayload{{1, 0}, 1, 0, false};
    auto preview = queue.submit(std::move(preview_request)).get();
    REQUIRE(preview);
    REQUIRE(preview.value().alignment_preview_image);
    CHECK_FALSE(preview.value().changes.project.has_value());
    CHECK(preview.value().changes.changed_chunks.empty());
    CHECK(preview.value().data.at("registration").at("offset") ==
          nlohmann::json::array({1, 0}));

    auto apply_request = request(
        chunkmap::CommandType::ChunkShiftApply,
        workspace, "alignment-apply", "world");
    apply_request.payload = chunkmap::ChunkAlignmentPayload{{1, 0}, 1, 0, false};
    auto applied = queue.submit(std::move(apply_request)).get();
    REQUIRE(applied);
    CHECK_FALSE(applied.value().changed_chunk_image);
    CHECK(applied.value().changes.changed_chunks.empty());
    REQUIRE(applied.value().project_snapshot);
    CHECK(applied.value().project_snapshot->layout.placement({1, 0}).offset_x == 1);
    CHECK(applied.value().project_snapshot->layout.placement({1, 0}).offset_y == 0);
    CHECK(applied.value().data.at("registration").at("offset") ==
          nlohmann::json::array({1, 0}));

    queue.stop_and_drain();
    std::filesystem::remove_all(workspace, error);
}

TEST_CASE("project grid command rebuilds an empty in-memory document") {
    const auto workspace = std::filesystem::temp_directory_path() /
        "chunkmap-project-grid-command";
    std::error_code error;
    std::filesystem::remove_all(workspace, error);

    chunkmap::DocumentCommandQueue queue;
    auto create = request(chunkmap::CommandType::ProjectCreate, workspace, "grid-1", "world");
    create.payload = chunkmap::ProjectCreatePayload{
        "world", std::filesystem::path(CHUNKMAP_TEST_SOURCE_DIR) / "fixtures/chunk.png",
        2, 2, 0.15, 0.15};
    REQUIRE(queue.submit(std::move(create)).get());

    auto resize = request(chunkmap::CommandType::ProjectGridSet, workspace, "grid-2", "world");
    resize.payload = chunkmap::ProjectGridSetPayload{4, 3};
    auto resized = queue.submit(std::move(resize)).get();
    REQUIRE(resized);
    REQUIRE(resized.value().project_snapshot);
    CHECK(resized.value().project_snapshot->config.columns == 4);
    CHECK(resized.value().project_snapshot->config.rows == 3);
    CHECK(resized.value().changes.project_changed);

    auto prompt = request(chunkmap::CommandType::PromptShow, workspace, "grid-3", "world");
    prompt.payload = chunkmap::CoordPayload{{3, 2}};
    auto shown = queue.submit(std::move(prompt)).get();
    REQUIRE(shown);
    CHECK(shown.value().data.at("prompt") == "");

    queue.stop_and_drain();
    std::filesystem::remove_all(workspace, error);
}

TEST_CASE("full map export command does not publish project changes") {
    const auto workspace = std::filesystem::temp_directory_path() /
        "chunkmap-map-export-command";
    std::error_code error;
    std::filesystem::remove_all(workspace, error);
    const auto fixture = std::filesystem::path(CHUNKMAP_TEST_SOURCE_DIR) / "fixtures/chunk.png";

    chunkmap::DocumentCommandQueue queue;
    auto create = request(chunkmap::CommandType::ProjectCreate, workspace, "export-1", "world");
    create.payload = chunkmap::ProjectCreatePayload{
        "world", fixture, 1, 1, 0.15, 0.15};
    REQUIRE(queue.submit(std::move(create)).get());
    auto import = request(chunkmap::CommandType::ChunkImport, workspace, "export-2", "world");
    import.payload = chunkmap::ChunkImagePayload{{0, 0}, fixture};
    REQUIRE(queue.submit(std::move(import)).get());

    const auto output = workspace / "full-map.png";
    auto export_request = request(
        chunkmap::CommandType::MapExport, workspace, "export-3", "world");
    export_request.payload = chunkmap::MapExportPayload{output, false};
    auto exported = queue.submit(std::move(export_request)).get();
    REQUIRE(exported);
    CHECK(std::filesystem::equivalent(
        std::filesystem::path(exported.value().data.at("output").get<std::string>()), output));
    CHECK_FALSE(exported.value().changes.project.has_value());
    CHECK_FALSE(exported.value().changes.project_changed);
    CHECK(exported.value().changes.changed_chunks.empty());
    CHECK(std::filesystem::is_regular_file(output));
    const auto updates = queue.take_updates();
    std::vector<chunkmap::CommandProgress> export_progress;
    for (const auto& progress : updates.progress) {
        if (progress.request_id == "export-3") export_progress.push_back(progress);
    }
    REQUIRE_FALSE(export_progress.empty());
    CHECK(export_progress.front().completed == 0U);
    CHECK(export_progress.back().completed == export_progress.back().total);
    CHECK(export_progress.back().message == "Export complete");

    queue.stop_and_drain();
    std::filesystem::remove_all(workspace, error);
}

TEST_CASE("concept slice export command writes only the requested external image") {
    const auto workspace = std::filesystem::temp_directory_path() /
        "chunkmap-concept-slice-export-command";
    std::error_code error;
    std::filesystem::remove_all(workspace, error);
    const auto fixture = std::filesystem::path(CHUNKMAP_TEST_SOURCE_DIR) / "fixtures/chunk.png";

    chunkmap::DocumentCommandQueue queue;
    auto create = request(
        chunkmap::CommandType::ProjectCreate, workspace, "concept-export-1", "world");
    create.payload = chunkmap::ProjectCreatePayload{
        "world", fixture, 2, 2, 0.15, 0.15};
    REQUIRE(queue.submit(std::move(create)).get());

    const auto output = workspace / "concept-1_0.png";
    auto export_request = request(
        chunkmap::CommandType::ConceptSliceExport,
        workspace, "concept-export-2", "world");
    export_request.payload = chunkmap::ConceptSliceExportPayload{{1, 0}, output, false};
    auto exported = queue.submit(std::move(export_request)).get();
    REQUIRE(exported);
    CHECK(exported.value().data.at("coord") == nlohmann::json::array({1, 0}));
    CHECK(std::filesystem::equivalent(
        std::filesystem::path(exported.value().data.at("output").get<std::string>()), output));
    CHECK_FALSE(exported.value().changes.project.has_value());
    CHECK(std::filesystem::is_regular_file(output));
    CHECK_FALSE(std::filesystem::exists(workspace / "output/world/concept/regions"));

    queue.stop_and_drain();
    std::filesystem::remove_all(workspace, error);
}

TEST_CASE("document command queue executes one FIFO write path") {
    const auto workspace = std::filesystem::temp_directory_path() /
        "chunkmap-phase6-command-queue";
    std::error_code error;
    std::filesystem::remove_all(workspace, error);

    chunkmap::DocumentCommandQueue queue;
    auto create = request(chunkmap::CommandType::ProjectCreate, workspace, "queue-1", "world");
    create.payload = chunkmap::ProjectCreatePayload{
        "world", std::filesystem::path(CHUNKMAP_TEST_SOURCE_DIR) / "fixtures/chunk.png",
        2, 2, 0.15, 0.15};
    auto set_first = request(chunkmap::CommandType::PromptSet, workspace, "queue-2", "world");
    set_first.payload = chunkmap::PromptSetPayload{{0, 0}, "first"};
    auto set_second = request(chunkmap::CommandType::PromptSet, workspace, "queue-3", "world");
    set_second.payload = chunkmap::PromptSetPayload{{0, 0}, "second"};
    auto show = request(chunkmap::CommandType::PromptShow, workspace, "queue-4", "world");
    show.payload = chunkmap::CoordPayload{{0, 0}};

    auto create_future = queue.submit(std::move(create));
    auto first_future = queue.submit(std::move(set_first));
    auto second_future = queue.submit(std::move(set_second));
    auto show_future = queue.submit(std::move(show));
    CHECK(create_future.get());
    CHECK(first_future.get());
    auto second_result = second_future.get();
    REQUIRE(second_result);
    CHECK(second_result.value().changes.changed_prompts ==
          std::vector<chunkmap::ChunkCoord>{{0, 0}});
    auto shown = show_future.get();
    REQUIRE(shown);
    CHECK(shown.value().data.at("prompt") == "second");

    const auto completions = queue.take_completions();
    REQUIRE(completions.size() == 4U);
    CHECK(completions[0].request.request_id == "queue-1");
    CHECK(completions[3].request.request_id == "queue-4");
    for (const auto& completion : completions) {
        CHECK(completion.queue_wait_ms >= 0.0);
        CHECK(completion.execution_ms >= 0.0);
    }
    queue.stop_and_drain();
    std::filesystem::remove_all(workspace, error);
}

TEST_CASE("project open publishes a full project switch") {
    const auto workspace = std::filesystem::temp_directory_path() /
        "chunkmap-project-open";
    std::error_code error;
    std::filesystem::remove_all(workspace, error);

    chunkmap::DocumentCommandQueue queue;
    auto create = request(chunkmap::CommandType::ProjectCreate, workspace, "open-1", "world");
    create.payload = chunkmap::ProjectCreatePayload{
        "world", std::filesystem::path(CHUNKMAP_TEST_SOURCE_DIR) / "fixtures/chunk.png",
        2, 2, 0.15, 0.15};
    REQUIRE(queue.submit(std::move(create)).get());

    auto open = request(chunkmap::CommandType::ProjectOpen, workspace, "open-2", "world");
    auto opened = queue.submit(std::move(open)).get();
    REQUIRE(opened);
    REQUIRE(opened.value().project_snapshot.has_value());
    CHECK(opened.value().project_snapshot->config.name == "world");
    CHECK(opened.value().changes.project_changed);
    REQUIRE(opened.value().changes.project.has_value());
    CHECK(opened.value().changes.project->project_name == "world");

    queue.stop_and_drain();
    std::filesystem::remove_all(workspace, error);
}

TEST_CASE("project current tracks explicit project switches instead of session loads") {
    const auto workspace = std::filesystem::temp_directory_path() /
        "chunkmap-project-current";
    std::error_code error;
    std::filesystem::remove_all(workspace, error);

    chunkmap::DocumentCommandQueue queue;
    auto current = request(
        chunkmap::CommandType::ProjectCurrent, workspace, "current-1");
    auto empty = queue.submit(current).get();
    REQUIRE_FALSE(empty);
    CHECK(empty.error().code == "no_project_open");

    for (const auto& name : {"world-a", "world-b"}) {
        auto create = request(
            chunkmap::CommandType::ProjectCreate, workspace,
            std::string("create-") + name, name);
        create.payload = chunkmap::ProjectCreatePayload{
            name, std::filesystem::path(CHUNKMAP_TEST_SOURCE_DIR) / "fixtures/chunk.png",
            2, 2, 0.15, 0.15};
        REQUIRE(queue.submit(std::move(create)).get());
    }

    auto open = request(
        chunkmap::CommandType::ProjectOpen, workspace, "current-2", "world-a");
    REQUIRE(queue.submit(std::move(open)).get());

    auto other_status = request(
        chunkmap::CommandType::ProjectStatus, workspace, "current-3", "world-b");
    REQUIRE(queue.submit(std::move(other_status)).get());

    current.request_id = "current-4";
    auto active = queue.submit(std::move(current)).get();
    REQUIRE(active);
    CHECK(active.value().data.at("name") == "world-a");
    CHECK(active.value().data.at("workspace") ==
          chunkmap::make_project_key(workspace, "world-a").workspace.string());
    REQUIRE(active.value().changes.project.has_value());
    CHECK(active.value().changes.project->project_name == "world-a");

    queue.stop_and_drain();
    std::filesystem::remove_all(workspace, error);
}

TEST_CASE("document reads stay in memory until explicit project reload") {
    const auto workspace = std::filesystem::temp_directory_path() /
        "chunkmap-session-memory";
    std::error_code error;
    std::filesystem::remove_all(workspace, error);

    chunkmap::DocumentCommandQueue queue;
    auto create = request(chunkmap::CommandType::ProjectCreate, workspace, "memory-1", "world");
    create.payload = chunkmap::ProjectCreatePayload{
        "world", std::filesystem::path(CHUNKMAP_TEST_SOURCE_DIR) / "fixtures/chunk.png",
        2, 2, 0.15, 0.15};
    REQUIRE(queue.submit(std::move(create)).get());

    auto set = request(chunkmap::CommandType::PromptSet, workspace, "memory-2", "world");
    set.payload = chunkmap::PromptSetPayload{{0, 0}, "session value"};
    REQUIRE(queue.submit(std::move(set)).get());
    REQUIRE(chunkmap::atomic_file::write_text(
        workspace / "output/world/prompts/0_0.md", "external value").ok());

    auto show = request(chunkmap::CommandType::PromptShow, workspace, "memory-3", "world");
    show.payload = chunkmap::CoordPayload{{0, 0}};
    auto before_reload = queue.submit(show).get();
    REQUIRE(before_reload);
    CHECK(before_reload.value().data.at("prompt") == "session value");

    auto reload = request(chunkmap::CommandType::ProjectOpen, workspace, "memory-4", "world");
    REQUIRE(queue.submit(std::move(reload)).get());
    show.request_id = "memory-5";
    auto after_reload = queue.submit(std::move(show)).get();
    REQUIRE(after_reload);
    CHECK(after_reload.value().data.at("prompt") == "external value");

    queue.stop_and_drain();
    std::filesystem::remove_all(workspace, error);
}

TEST_CASE("derived reads reject uncached external image changes until reload") {
    const auto workspace = std::filesystem::temp_directory_path() /
        "chunkmap-session-external-image";
    std::error_code error;
    std::filesystem::remove_all(workspace, error);
    const auto fixture = std::filesystem::path(CHUNKMAP_TEST_SOURCE_DIR) / "fixtures/chunk.png";

    chunkmap::DocumentCommandQueue queue;
    auto create = request(chunkmap::CommandType::ProjectCreate, workspace, "external-1", "world");
    create.payload = chunkmap::ProjectCreatePayload{"world", fixture, 2, 2, 0.15, 0.15};
    REQUIRE(queue.submit(std::move(create)).get());
    auto import = request(chunkmap::CommandType::ChunkImport, workspace, "external-2", "world");
    import.payload = chunkmap::ChunkImagePayload{{1, 1}, fixture};
    REQUIRE(queue.submit(std::move(import)).get());
    auto reload = request(chunkmap::CommandType::ProjectOpen, workspace, "external-3", "world");
    REQUIRE(queue.submit(std::move(reload)).get());

    auto replacement = chunkmap::atomic_file::read_binary(fixture);
    REQUIRE(replacement.ok());
    REQUIRE(chunkmap::atomic_file::write_binary(
        workspace / "output/world/chunks/1_1.png", replacement.value()).ok());
    auto context = request(chunkmap::CommandType::ChunkContext, workspace, "external-4", "world");
    context.payload = chunkmap::CoordPayload{{0, 1}};
    auto rejected = queue.submit(context).get();
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == "external_project_change");

    reload = request(chunkmap::CommandType::ProjectOpen, workspace, "external-5", "world");
    REQUIRE(queue.submit(std::move(reload)).get());
    context.request_id = "external-6";
    REQUIRE(queue.submit(std::move(context)).get());
    queue.stop_and_drain();
    std::filesystem::remove_all(workspace, error);
}

TEST_CASE("desktop command host is single instance") {
    chunkmap::DocumentCommandQueue first_queue;
    chunkmap::DocumentCommandQueue second_queue;
    chunkmap::DesktopIpcServer first_server;
    chunkmap::DesktopIpcServer second_server;
    REQUIRE(first_server.start(first_queue));
    auto duplicate = second_server.start(second_queue);
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.error().code == "desktop_already_running");
    first_server.stop();
    first_queue.stop_and_drain();
    second_queue.stop_and_drain();
}
