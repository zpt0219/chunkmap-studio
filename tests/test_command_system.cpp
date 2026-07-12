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

    auto second = request(chunkmap::CommandType::ChunkImport, workspace, "global-3", "world");
    second.payload = chunkmap::ChunkImagePayload{
        {1, 1}, std::filesystem::path(CHUNKMAP_TEST_SOURCE_DIR) / "fixtures/chunk.png"};
    auto second_result = queue.submit(std::move(second)).get();
    REQUIRE(second_result);
    CHECK_FALSE(second_result.value().data.contains("global_prompt_action"));

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
