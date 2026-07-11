#pragma once

#include "core/result.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace chunkmap_cli {

struct ParsedCommandLine {
    std::filesystem::path workspace;
    std::optional<std::string> project_name;
    bool json = false;
    std::vector<std::string> args;
};

chunkmap::Result<ParsedCommandLine> parse_command_line(int argc, char** argv);

std::optional<std::string> option_value(const std::vector<std::string>& args,
                                        const std::string& name);
bool has_flag(const std::vector<std::string>& args, const std::string& name);

}  // namespace chunkmap_cli

