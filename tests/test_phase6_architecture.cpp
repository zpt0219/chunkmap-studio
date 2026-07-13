#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string read_source(const std::filesystem::path& relative) {
    std::ifstream input(std::filesystem::path(CHUNKMAP_SOURCE_DIR) / relative);
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

}  // namespace

TEST_CASE("phase 6 has no alternate CLI or watcher write path") {
    const auto cli = read_source("cli/src/cli_app.cpp");
    const auto app = read_source("desktop/src/app.cpp");
    const auto service = read_source("src/project/project_service.cpp");
    CHECK(cli.find("ProjectService") == std::string::npos);
    CHECK(app.find("ProjectService") == std::string::npos);
    CHECK(app.find("ProjectWatcher") == std::string::npos);
    CHECK(service.find("append_event") == std::string::npos);
    CHECK(service.find("events_jsonl") == std::string::npos);
}

TEST_CASE("desktop menu keeps global actions in the app shell") {
    const auto app = read_source("desktop/src/app.cpp");
    const auto header = read_source("desktop/src/app.h");
    const auto main = read_source("desktop/src/main.cpp");
    CHECK(app.find("BeginMainMenuBar") != std::string::npos);
    CHECK(app.find("BeginMenu(\"File\"") != std::string::npos);
    CHECK(app.find("BeginMenu(\"Project\"") != std::string::npos);
    CHECK(app.find("BeginMenu(\"View\"") != std::string::npos);
    CHECK(app.find("void App::draw_map_controls()") != std::string::npos);
    CHECK(app.find("void App::draw_toolbar()") == std::string::npos);
    CHECK(header.find("bool exit_requested() const") != std::string::npos);
    CHECK(main.find("app.exit_requested()") != std::string::npos);
}

TEST_CASE("prompt inspector exposes global and local prompt sources") {
    const auto app = read_source("desktop/src/app.cpp");
    CHECK(app.find("SeparatorText(\"Global Prompt\")") != std::string::npos);
    CHECK(app.find("##global_prompt_inspector") != std::string::npos);
    CHECK(app.find("SeparatorText(\"Local Chunk Prompt\")") != std::string::npos);
    CHECK(app.find("##local_chunk_prompt_editor") != std::string::npos);
    CHECK(app.find("ImGuiInputTextFlags_WordWrap") != std::string::npos);
    CHECK(app.find("kPromptAutosaveDelaySeconds = 60.0") != std::string::npos);
    CHECK(app.find("Autosaves after 1 min idle") != std::string::npos);
}

TEST_CASE("chunk visibility is a desktop-only review control") {
    const auto app = read_source("desktop/src/app.cpp");
    const auto header = read_source("desktop/src/app.h");
    const auto repository = read_source("src/project/project_repository.cpp");
    CHECK(app.find("Checkbox(\"Visible on Map\"") != std::string::npos);
    CHECK(app.find("chunk_image_visible(coord)") != std::string::npos);
    CHECK(app.find("chunk_image_visibility_.assign") != std::string::npos);
    CHECK(header.find("chunk_image_visibility_") != std::string::npos);
    CHECK(repository.find("chunk_image_visibility") == std::string::npos);
}
