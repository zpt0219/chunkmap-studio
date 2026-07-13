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
    CHECK(app.find("Checkbox(\"Overlays\"") != std::string::npos);
    CHECK(app.find("Button(\"Reset Scale\"") != std::string::npos);
    CHECK(app.find("Button(\"Fit Map\"") != std::string::npos);
    CHECK(app.find("Scale: %.3fx") != std::string::npos);
    CHECK(app.find("ImGuiMouseButton_Middle") != std::string::npos);
    CHECK(app.find("std::pow(1.18F") == std::string::npos);
    CHECK(app.find("zoom_ > 0.18F") == std::string::npos);
    CHECK(header.find("MapZoomState map_view_") != std::string::npos);
    CHECK(header.find("show_overlays_") != std::string::npos);
    CHECK(header.find("show_grid_") == std::string::npos);
    CHECK(header.find("show_coordinates_") == std::string::npos);
    CHECK(header.find("show_seams_") == std::string::npos);
    CHECK(app.find("kLogWheelLines = 3.0F") != std::string::npos);
    CHECK(header.find("std::optional<float> log_scroll_y_") != std::string::npos);
    CHECK(app.find("CommandType::ProjectGridSet") != std::string::npos);
    CHECK(app.find("Button(\"Apply Grid\"") != std::string::npos);
    CHECK(app.find("MenuItem(\"Change Grid...\"") != std::string::npos);
    CHECK(app.find("void App::draw_change_grid_modal()") != std::string::npos);
    CHECK(header.find("change_grid_columns_") != std::string::npos);
    CHECK(header.find("bool exit_requested() const") != std::string::npos);
    CHECK(main.find("app.exit_requested()") != std::string::npos);
    CHECK(main.find("desktop_config_directory()") != std::string::npos);
    CHECK(main.find("window-state.ini") != std::string::npos);
    CHECK(main.find("SaveIniSettingsToDisk") != std::string::npos);
    CHECK(app.find("DockBuilderGetNode(dockspace_id) == nullptr") != std::string::npos);
    CHECK(header.find("reset_layout_requested_") != std::string::npos);
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

TEST_CASE("prompt authoring guide is mandatory and embedded into handoff") {
    const auto agents = read_source("AGENTS.md");
    const auto guide = read_source("docs/PROMPT_AUTHORING_GUIDE.md");
    const auto cmake = read_source("src/CMakeLists.txt");
    const auto service = read_source("src/project/project_service.cpp");
    CHECK(agents.find("docs/PROMPT_AUTHORING_GUIDE.md") != std::string::npos);
    CHECK(agents.find("This is mandatory") != std::string::npos);
    CHECK(agents.find("or generating any Chunk") != std::string::npos);
    CHECK(guide.find("Specification version: 2") != std::string::npos);
    CHECK(guide.find("Preserve model freedom") != std::string::npos);
    CHECK(guide.find("Interpret Concept symbols semantically") != std::string::npos);
    CHECK(guide.find("gameplay-ready overworld tilemap") != std::string::npos);
    CHECK(guide.find("Generation-time Prompt discipline") != std::string::npos);
    CHECK(guide.find("A Seam difference of `0.0`") != std::string::npos);
    CHECK(guide.find("Regenerating adjacent drifted Chunks") != std::string::npos);
    CHECK(cmake.find("file(READ \"${CMAKE_SOURCE_DIR}/docs/PROMPT_AUTHORING_GUIDE.md\"") !=
          std::string::npos);
    CHECK(service.find("export_prompt_authoring_guide") != std::string::npos);
    CHECK(service.find("authoring_guide") != std::string::npos);
}

TEST_CASE("concept comparison is a momentary desktop-only review control") {
    const auto app = read_source("desktop/src/app.cpp");
    const auto repository = read_source("src/project/project_repository.cpp");
    CHECK(app.find("Button(\"Hold: This Chunk\"") != std::string::npos);
    CHECK(app.find("Button(\"Hold: Full Map\"") != std::string::npos);
    CHECK(app.find("Button(\"Export Concept Slice...\"") != std::string::npos);
    CHECK(repository.find("compare_full_concept") == std::string::npos);
}
