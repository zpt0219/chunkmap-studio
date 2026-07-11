#include "command_parser.h"

#include <utility>

namespace chunkmap_cli {

chunkmap::Result<ParsedCommandLine> parse_command_line(int argc, char** argv) {
    ParsedCommandLine parsed;
    parsed.workspace = std::filesystem::current_path();

    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--json") {
            parsed.json = true;
        } else if (argument == "--workspace" || argument == "--project") {
            if (index + 1 >= argc) {
                return chunkmap::Result<ParsedCommandLine>::failure(
                    "missing_option_value", "Missing value for " + argument);
            }
            const std::string value(argv[++index]);
            if (argument == "--workspace") parsed.workspace = value;
            else parsed.project_name = value;
        } else {
            parsed.args.push_back(argument);
        }
    }

    return chunkmap::Result<ParsedCommandLine>::success(std::move(parsed));
}

std::optional<std::string> option_value(const std::vector<std::string>& args,
                                        const std::string& name) {
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (args[index] == name && index + 1U < args.size()) return args[index + 1U];
    }
    return std::nullopt;
}

bool has_flag(const std::vector<std::string>& args, const std::string& name) {
    for (const auto& argument : args) {
        if (argument == name) return true;
    }
    return false;
}

}  // namespace chunkmap_cli

