#include "command/command_codec.h"
#include "command/document_command_queue.h"
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

TEST_CASE("document command queue executes one FIFO write path") {
    const auto workspace = std::filesystem::temp_directory_path() /
        "chunkmap-phase6-command-queue";
    std::error_code error;
    std::filesystem::remove_all(workspace, error);

    chunkmap::DocumentCommandQueue queue;
    auto create = request(chunkmap::CommandType::ProjectCreate, workspace, "queue-1", "world");
    create.payload = chunkmap::ProjectCreatePayload{
        "world", std::filesystem::path(CHUNKMAP_TEST_SOURCE_DIR) / "fixtures/chunk.png",
        2, 2, 0.15, 0.15, 0.03};
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
