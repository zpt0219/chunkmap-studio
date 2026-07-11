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
