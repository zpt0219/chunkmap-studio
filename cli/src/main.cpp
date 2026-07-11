#include "cli_app.h"
#include "command_parser.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <iostream>
#include <string_view>

namespace {

bool wants_json(int argc, char** argv) {
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == "--json") return true;
    }
    return false;
}

int startup_error(bool json,
                  const std::string& command,
                  const std::string& code,
                  const std::string& message) {
    if (json) {
        std::cout << nlohmann::json{
            {"schema_version", 1},
            {"ok", false},
            {"command", command},
            {"project", nullptr},
            {"error", {{"code", code}, {"message", message}}}}.dump() << '\n';
    } else {
        std::cerr << message << '\n';
    }
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    const bool json = wants_json(argc, argv);
    try {
        auto parsed = chunkmap_cli::parse_command_line(argc, argv);
        if (!parsed) {
            return startup_error(json, "usage", parsed.error().code, parsed.error().message);
        }
        return chunkmap_cli::CliApp(std::move(parsed.value())).run();
    } catch (const std::exception& exception) {
        return startup_error(json, "internal", "internal_error", exception.what());
    } catch (...) {
        return startup_error(json, "internal", "internal_error", "Unknown internal error.");
    }
}
