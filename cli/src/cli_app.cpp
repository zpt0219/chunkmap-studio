#include "cli_app.h"

#include "core/version.h"
#include "io/atomic_file.h"
#include "ipc/desktop_ipc.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace chunkmap_cli {

namespace {

chunkmap::Result<int> parse_positive_int(const std::string& text, const std::string& name) {
    try {
        std::size_t used = 0;
        const int value = std::stoi(text, &used);
        if (used != text.size() || value <= 0) throw std::invalid_argument("invalid");
        return chunkmap::Result<int>::success(value);
    } catch (const std::exception&) {
        return chunkmap::Result<int>::failure("invalid_integer", name + " must be a positive integer.");
    }
}

chunkmap::Result<double> parse_double(const std::string& text, const std::string& name) {
    try {
        std::size_t used = 0;
        const double value = std::stod(text, &used);
        if (used != text.size()) throw std::invalid_argument("invalid");
        return chunkmap::Result<double>::success(value);
    } catch (const std::exception&) {
        return chunkmap::Result<double>::failure("invalid_number", name + " must be a number.");
    }
}

chunkmap::Result<std::pair<double, double>> parse_ratio_pair(const std::string& text) {
    const auto separator = text.find('x');
    if (separator == std::string::npos) {
        return chunkmap::Result<std::pair<double, double>>::failure(
            "invalid_ratio_pair", "Overlap ratio must use horizontalxvertical format.");
    }
    auto horizontal = parse_double(text.substr(0, separator), "horizontal overlap ratio");
    auto vertical = parse_double(text.substr(separator + 1U), "vertical overlap ratio");
    if (!horizontal) return chunkmap::Result<std::pair<double, double>>::failure(
        horizontal.error().code, horizontal.error().message);
    if (!vertical) return chunkmap::Result<std::pair<double, double>>::failure(
        vertical.error().code, vertical.error().message);
    return chunkmap::Result<std::pair<double, double>>::success(
        {horizontal.value(), vertical.value()});
}

std::filesystem::path absolute_path(const std::filesystem::path& path) {
    return std::filesystem::absolute(path).lexically_normal();
}

std::string next_request_id() {
    static std::atomic<unsigned long long> sequence{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return "cli-" + std::to_string(ticks) + "-" + std::to_string(sequence.fetch_add(1));
}

}  // namespace

CliApp::CliApp(ParsedCommandLine command_line)
    : command_line_(std::move(command_line)) {
    command_line_.workspace = absolute_path(command_line_.workspace);
}

int CliApp::run() {
    if (command_line_.args.empty() || command_line_.args[0] == "--help" ||
        command_line_.args[0] == "-h") {
        print_help();
        return 0;
    }
    if (command_line_.args[0] == "--version") {
        return print_success("version", {{"version", std::string(chunkmap::version())}},
                             "chunkmap " + std::string(chunkmap::version()));
    }
    if (command_line_.args[0] == "project") return run_project();
    if (command_line_.args[0] == "prompt") return run_prompt();
    if (command_line_.args[0] == "prompts") return run_prompts();
    if (command_line_.args[0] == "global-prompt") return run_global_prompt();
    if (command_line_.args[0] == "concept") return run_concept();
    if (command_line_.args[0] == "chunk") return run_chunk();
    if (command_line_.args[0] == "seam") return run_seam();
    if (command_line_.args[0] == "map") return run_map();
    return usage_error("Unknown command: " + command_line_.args[0]);
}

chunkmap::CommandRequest CliApp::make_request(chunkmap::CommandType type) const {
    chunkmap::CommandRequest request;
    request.request_id = next_request_id();
    request.type = type;
    request.workspace = command_line_.workspace;
    request.project_name = command_line_.project_name;
    return request;
}

int CliApp::execute(chunkmap::CommandRequest request) {
    chunkmap::DesktopIpcClient client;
    auto reply = client.send(request);
    if (!reply) return print_error(chunkmap::command_name(request.type), reply.error());
    if (command_line_.json) {
        std::cout << reply.value().envelope.dump() << '\n';
    } else if (!reply.value().text.empty()) {
        const bool ok = reply.value().envelope.value("ok", false);
        (ok ? std::cout : std::cerr) << reply.value().text << '\n';
    }
    return reply.value().envelope.value("ok", false) ? 0 : 1;
}

int CliApp::run_project() {
    if (command_line_.args.size() < 2U) return usage_error("Missing project subcommand.");
    const auto& action = command_line_.args[1];
    if (action == "init") {
        if (command_line_.args.size() < 3U) return usage_error("project init requires a project name.");
        const auto concept = option_value(command_line_.args, "--concept");
        const auto columns_text = option_value(command_line_.args, "--columns");
        const auto rows_text = option_value(command_line_.args, "--rows");
        if (!concept || !columns_text || !rows_text) {
            return usage_error("project init requires --concept, --columns and --rows.");
        }
        auto columns = parse_positive_int(*columns_text, "columns");
        auto rows = parse_positive_int(*rows_text, "rows");
        if (!columns) return print_error("project init", columns.error());
        if (!rows) return print_error("project init", rows.error());
        chunkmap::ProjectCreatePayload payload;
        payload.name = command_line_.args[2];
        payload.concept_image = absolute_path(*concept);
        payload.columns = columns.value();
        payload.rows = rows.value();
        if (const auto overlap = option_value(command_line_.args, "--overlap-ratio")) {
            auto ratios = parse_ratio_pair(*overlap);
            if (!ratios) return print_error("project init", ratios.error());
            payload.horizontal_overlap_ratio = ratios.value().first;
            payload.vertical_overlap_ratio = ratios.value().second;
        }
        auto request = make_request(chunkmap::CommandType::ProjectCreate);
        request.project_name = payload.name;
        request.payload = std::move(payload);
        return execute(std::move(request));
    }
    if (action == "open") {
        if (command_line_.args.size() != 3U) {
            return usage_error("project open requires exactly one project name.");
        }
        auto request = make_request(chunkmap::CommandType::ProjectOpen);
        request.project_name = command_line_.args[2];
        return execute(std::move(request));
    }
    if (action == "current") {
        if (command_line_.args.size() != 2U) {
            return usage_error("project current does not accept arguments.");
        }
        if (command_line_.project_name) {
            return usage_error("project current does not accept --project.");
        }
        return execute(make_request(chunkmap::CommandType::ProjectCurrent));
    }
    if (action == "grid") {
        auto project = require_project();
        if (!project) return print_error("project grid", project.error());
        const auto columns_text = option_value(command_line_.args, "--columns");
        const auto rows_text = option_value(command_line_.args, "--rows");
        if (!columns_text || !rows_text) {
            return usage_error("project grid requires --columns and --rows.");
        }
        auto columns = parse_positive_int(*columns_text, "columns");
        auto rows = parse_positive_int(*rows_text, "rows");
        if (!columns) return print_error("project grid", columns.error());
        if (!rows) return print_error("project grid", rows.error());
        auto request = make_request(chunkmap::CommandType::ProjectGridSet);
        request.payload = chunkmap::ProjectGridSetPayload{columns.value(), rows.value()};
        return execute(std::move(request));
    }
    if (action != "status" && action != "validate") {
        return usage_error("Unknown project subcommand: " + action);
    }
    auto project = require_project();
    if (!project) return print_error("project " + action, project.error());
    return execute(make_request(action == "status" ? chunkmap::CommandType::ProjectStatus
                                                    : chunkmap::CommandType::ProjectValidate));
}

int CliApp::run_prompt() {
    if (command_line_.args.size() < 3U) return usage_error("prompt requires show/set and x,y.");
    auto project = require_project();
    if (!project) return print_error("prompt", project.error());
    auto coord = parse_coord(command_line_.args[2]);
    if (!coord) return print_error("prompt", coord.error());
    const auto& action = command_line_.args[1];
    if (action == "show") {
        auto request = make_request(chunkmap::CommandType::PromptShow);
        request.payload = chunkmap::CoordPayload{coord.value()};
        return execute(std::move(request));
    }
    if (action != "set") return usage_error("Unknown prompt subcommand: " + action);
    const auto file = option_value(command_line_.args, "--file");
    if (!file) return usage_error("prompt set requires --file.");
    auto content = chunkmap::atomic_file::read_text(*file);
    if (!content) return print_error("prompt set", content.error());
    auto request = make_request(chunkmap::CommandType::PromptSet);
    request.payload = chunkmap::PromptSetPayload{coord.value(), content.take_value()};
    return execute(std::move(request));
}

int CliApp::run_prompts() {
    if (command_line_.args.size() < 2U || command_line_.args[1] != "import") {
        return usage_error("prompts supports only the import subcommand.");
    }
    auto project = require_project();
    if (!project) return print_error("prompts import", project.error());
    const auto input = option_value(command_line_.args, "--input");
    if (!input) return usage_error("prompts import requires --input.");
    auto request = make_request(chunkmap::CommandType::PromptsImport);
    request.payload = chunkmap::PathPayload{absolute_path(*input)};
    return execute(std::move(request));
}

int CliApp::run_global_prompt() {
    if (command_line_.args.size() < 2U) {
        return usage_error("global-prompt requires show or set.");
    }
    auto project = require_project();
    if (!project) return print_error("global-prompt", project.error());
    const auto& action = command_line_.args[1];
    if (action == "show") {
        return execute(make_request(chunkmap::CommandType::GlobalPromptShow));
    }
    if (action != "set") {
        return usage_error("Unknown global-prompt subcommand: " + action);
    }
    const auto file = option_value(command_line_.args, "--file");
    if (!file) return usage_error("global-prompt set requires --file.");
    auto content = chunkmap::atomic_file::read_text(*file);
    if (!content) return print_error("global-prompt set", content.error());
    auto request = make_request(chunkmap::CommandType::GlobalPromptSet);
    request.payload = chunkmap::GlobalPromptSetPayload{content.take_value()};
    return execute(std::move(request));
}

int CliApp::run_concept() {
    if (command_line_.args.size() < 2U || command_line_.args[1] != "context") {
        return usage_error("concept supports only the context subcommand.");
    }
    auto project = require_project();
    if (!project) return print_error("concept context", project.error());
    return execute(make_request(chunkmap::CommandType::ConceptContext));
}

int CliApp::run_chunk() {
    if (command_line_.args.size() < 3U) {
        return usage_error("chunk requires import/context/write/show/remove and x,y.");
    }
    const auto& action = command_line_.args[1];
    auto coord = parse_coord(command_line_.args[2]);
    if (!coord) return print_error("chunk " + action, coord.error());
    auto project = require_project();
    if (!project) return print_error("chunk " + action, project.error());
    chunkmap::CommandType type;
    if (action == "context") type = chunkmap::CommandType::ChunkContext;
    else if (action == "show") type = chunkmap::CommandType::ChunkShow;
    else if (action == "remove") {
        if (!has_flag(command_line_.args, "--yes")) return usage_error("chunk remove requires --yes.");
        type = chunkmap::CommandType::ChunkRemove;
    } else if (action == "import" || action == "write") {
        const auto image = option_value(command_line_.args, "--image");
        if (!image) return usage_error("chunk " + action + " requires --image.");
        auto request = make_request(action == "import"
            ? chunkmap::CommandType::ChunkImport : chunkmap::CommandType::ChunkWrite);
        request.payload = chunkmap::ChunkImagePayload{coord.value(), absolute_path(*image)};
        return execute(std::move(request));
    } else {
        return usage_error("Unknown chunk subcommand: " + action);
    }
    auto request = make_request(type);
    request.payload = chunkmap::CoordPayload{coord.value()};
    return execute(std::move(request));
}

int CliApp::run_seam() {
    if (command_line_.args.size() < 3U || command_line_.args[1] != "inspect") {
        return usage_error("seam inspect requires x,y and --direction right|bottom.");
    }
    auto coord = parse_coord(command_line_.args[2]);
    if (!coord) return print_error("seam inspect", coord.error());
    const auto direction = option_value(command_line_.args, "--direction");
    if (!direction || (*direction != "right" && *direction != "bottom")) {
        return usage_error("seam inspect requires --direction right|bottom.");
    }
    auto project = require_project();
    if (!project) return print_error("seam inspect", project.error());
    auto request = make_request(chunkmap::CommandType::SeamInspect);
    request.payload = chunkmap::SeamInspectPayload{
        coord.value(), *direction == "right" ? chunkmap::CommandSeamDirection::Right
                                              : chunkmap::CommandSeamDirection::Bottom};
    return execute(std::move(request));
}

int CliApp::run_map() {
    if (command_line_.args.size() < 3U || command_line_.args[1] != "export") {
        return usage_error("map export requires an absolute PNG output path.");
    }
    if (command_line_.args.size() > 4U ||
        (command_line_.args.size() == 4U && command_line_.args[3] != "--force")) {
        return usage_error("map export accepts only an output path and optional --force.");
    }
    auto project = require_project();
    if (!project) return print_error("map export", project.error());
    auto request = make_request(chunkmap::CommandType::MapExport);
    request.payload = chunkmap::MapExportPayload{
        std::filesystem::path(command_line_.args[2]), has_flag(command_line_.args, "--force")};
    return execute(std::move(request));
}

int CliApp::print_success(const std::string& command, nlohmann::json data, std::string text) {
    if (command_line_.json) {
        std::cout << nlohmann::json{{"schema_version", 1}, {"ok", true},
            {"command", command}, {"project", command_line_.project_name
                ? nlohmann::json(*command_line_.project_name) : nlohmann::json(nullptr)},
            {"data", std::move(data)}}.dump() << '\n';
    } else if (!text.empty()) {
        std::cout << text << '\n';
    }
    return 0;
}

int CliApp::print_error(const std::string& command, const chunkmap::Error& error) {
    if (command_line_.json) {
        std::cout << nlohmann::json{{"schema_version", 1}, {"ok", false},
            {"command", command}, {"project", command_line_.project_name
                ? nlohmann::json(*command_line_.project_name) : nlohmann::json(nullptr)},
            {"error", {{"code", error.code}, {"message", error.message}}}}.dump() << '\n';
    } else {
        std::cerr << error.message << '\n';
    }
    return 1;
}

int CliApp::usage_error(const std::string& message) {
    return print_error("usage", {"invalid_usage", message});
}

chunkmap::Result<std::string> CliApp::require_project() const {
    if (!command_line_.project_name) {
        return chunkmap::Result<std::string>::failure(
            "missing_project", "This command requires --project <name>.");
    }
    return chunkmap::Result<std::string>::success(*command_line_.project_name);
}

chunkmap::Result<chunkmap::ChunkCoord> CliApp::parse_coord(const std::string& value) const {
    const auto separator = value.find(',');
    if (separator == std::string::npos) {
        return chunkmap::Result<chunkmap::ChunkCoord>::failure(
            "invalid_coord", "Chunk coordinate must use x,y format.");
    }
    try {
        std::size_t x_used = 0;
        std::size_t y_used = 0;
        const auto x_text = value.substr(0, separator);
        const auto y_text = value.substr(separator + 1U);
        const int x = std::stoi(x_text, &x_used);
        const int y = std::stoi(y_text, &y_used);
        if (x_used != x_text.size() || y_used != y_text.size()) throw std::invalid_argument("invalid");
        return chunkmap::Result<chunkmap::ChunkCoord>::success({x, y});
    } catch (const std::exception&) {
        return chunkmap::Result<chunkmap::ChunkCoord>::failure(
            "invalid_coord", "Chunk coordinate must use integer x,y format.");
    }
}

void CliApp::print_help() const {
    std::cout
        << "chunkmap - AI chunk map Desktop command client\n\n"
        << "Desktop must be running before project commands are used.\n\n"
        << "Global options:\n"
        << "  --workspace <path>\n  --project <name>\n  --json\n\n"
        << "Commands:\n"
        << "  project init <name> --concept <image> --columns <n> --rows <n>\n"
        << "  project open <name>\n"
        << "  project current\n"
        << "  project grid --columns <n> --rows <n>\n"
        << "  project status\n  project validate\n"
        << "  prompt show <x,y>\n  prompt set <x,y> --file <path>\n"
        << "  prompts import --input <json>\n  concept context\n"
        << "  global-prompt show\n  global-prompt set --file <path>\n"
        << "  chunk import <x,y> --image <path>\n"
        << "  chunk context <x,y>\n  chunk write <x,y> --image <path>\n"
        << "  chunk show <x,y>\n  chunk remove <x,y> --yes\n"
        << "  seam inspect <x,y> --direction right|bottom\n"
        << "  map export <absolute-output.png> [--force]\n"
        << "  --version\n";
}

}  // namespace chunkmap_cli
