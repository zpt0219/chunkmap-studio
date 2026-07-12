#pragma once

#include "command_parser.h"

#include "command/command_request.h"
#include "core/result.h"
#include "model/chunk_coord.h"

#include <nlohmann/json.hpp>

#include <string>

namespace chunkmap_cli {

class CliApp {
public:
    explicit CliApp(ParsedCommandLine command_line);
    int run();

private:
    int run_project();
    int run_prompt();
    int run_prompts();
    int run_global_prompt();
    int run_concept();
    int run_chunk();
    int run_render();
    int run_seam();
    int run_map();

    int execute(chunkmap::CommandRequest request);
    chunkmap::CommandRequest make_request(chunkmap::CommandType type) const;

    int print_success(const std::string& command,
                      nlohmann::json data,
                      std::string text = {});
    int print_error(const std::string& command, const chunkmap::Error& error);
    int usage_error(const std::string& message);

    chunkmap::Result<std::string> require_project() const;
    chunkmap::Result<chunkmap::ChunkCoord> parse_coord(const std::string& value) const;

    void print_help() const;

    ParsedCommandLine command_line_;
};

}  // namespace chunkmap_cli
