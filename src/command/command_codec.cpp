#include "command/command_codec.h"

#include <exception>
#include <stdexcept>
#include <type_traits>

namespace chunkmap {

namespace {

using nlohmann::json;

Result<CommandType> parse_type(const std::string& name) {
    for (const auto type : {
             CommandType::ProjectCreate, CommandType::ProjectOpen,
             CommandType::ProjectStatus, CommandType::ProjectValidate,
             CommandType::ConceptContext, CommandType::PromptsImport,
             CommandType::PromptShow, CommandType::PromptSet,
             CommandType::ChunkImport, CommandType::ChunkContext, CommandType::ChunkWrite,
             CommandType::ChunkShow, CommandType::ChunkRemove,
             CommandType::Render, CommandType::SeamInspect,
             CommandType::MapExport}) {
        if (command_name(type) == name) return Result<CommandType>::success(type);
    }
    return Result<CommandType>::failure("unknown_command", "Unknown command: " + name);
}

json coord_value(ChunkCoord coord) {
    return json::array({coord.x, coord.y});
}

ChunkCoord parse_coord(const json& value) {
    if (!value.is_array() || value.size() != 2U) {
        throw std::invalid_argument("coord must be [x,y]");
    }
    return {value.at(0).get<int>(), value.at(1).get<int>()};
}

json changes_json(const ChangeSet& changes) {
    json chunks = json::array();
    for (const auto coord : changes.changed_chunks) chunks.push_back(coord_value(coord));
    json prompts = json::array();
    for (const auto coord : changes.changed_prompts) prompts.push_back(coord_value(coord));
    json seams = json::array();
    for (const auto& seam : changes.changed_seams) {
        seams.push_back({{"coord", coord_value(seam.coord)},
                         {"direction", seam.direction == SeamDirection::Right ? "right" : "bottom"}});
    }
    json contexts = json::array();
    for (const auto& path : changes.changed_contexts) contexts.push_back(path.string());
    return {
        {"project_changed", changes.project_changed},
        {"composite_changed", changes.composite_changed},
        {"concept_changed", changes.concept_changed},
        {"changed_chunks", chunks},
        {"changed_prompts", prompts},
        {"changed_seams", seams},
        {"changed_contexts", contexts},
    };
}

}  // namespace

json encode_command_request(const CommandRequest& request) {
    json payload = json::object();
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ProjectCreatePayload>) {
            payload = {{"name", value.name}, {"concept", value.concept_image.string()},
                       {"columns", value.columns}, {"rows", value.rows},
                       {"overlap_ratio", {value.horizontal_overlap_ratio,
                                          value.vertical_overlap_ratio}},
                       {"feather_ratio", value.feather_ratio}};
        } else if constexpr (std::is_same_v<T, CoordPayload>) {
            payload = {{"coord", coord_value(value.coord)}};
        } else if constexpr (std::is_same_v<T, PromptSetPayload>) {
            payload = {{"coord", coord_value(value.coord)}, {"text", value.text}};
        } else if constexpr (std::is_same_v<T, PathPayload>) {
            payload = {{"path", value.path.string()}};
        } else if constexpr (std::is_same_v<T, ChunkImagePayload>) {
            payload = {{"coord", coord_value(value.coord)}, {"image", value.image.string()}};
        } else if constexpr (std::is_same_v<T, SeamInspectPayload>) {
            payload = {{"coord", coord_value(value.coord)},
                       {"direction", value.direction == CommandSeamDirection::Right
                           ? "right" : "bottom"}};
        }
    }, request.payload);
    return {
        {"protocol_version", 1},
        {"request_id", request.request_id},
        {"command", command_name(request.type)},
        {"workspace", request.workspace.string()},
        {"project", request.project_name ? json(*request.project_name) : json(nullptr)},
        {"payload", payload},
    };
}

Result<CommandRequest> decode_command_request(const json& value) {
    try {
        if (value.at("protocol_version").get<int>() != 1) {
            return Result<CommandRequest>::failure(
                "unsupported_ipc_protocol", "Only IPC protocol version 1 is supported.");
        }
        auto type = parse_type(value.at("command").get<std::string>());
        if (!type) return Result<CommandRequest>::failure(type.error().code, type.error().message);
        CommandRequest request;
        request.request_id = value.at("request_id").get<std::string>();
        request.type = type.value();
        request.workspace = value.at("workspace").get<std::string>();
        if (!value.at("project").is_null()) {
            request.project_name = value.at("project").get<std::string>();
        }
        const auto& payload = value.at("payload");
        switch (request.type) {
        case CommandType::ProjectCreate: {
            ProjectCreatePayload parsed;
            parsed.name = payload.at("name").get<std::string>();
            parsed.concept_image = payload.at("concept").get<std::string>();
            parsed.columns = payload.at("columns").get<int>();
            parsed.rows = payload.at("rows").get<int>();
            parsed.horizontal_overlap_ratio = payload.at("overlap_ratio").at(0).get<double>();
            parsed.vertical_overlap_ratio = payload.at("overlap_ratio").at(1).get<double>();
            parsed.feather_ratio = payload.at("feather_ratio").get<double>();
            request.payload = std::move(parsed);
            break;
        }
        case CommandType::PromptSet:
            request.payload = PromptSetPayload{
                parse_coord(payload.at("coord")), payload.at("text").get<std::string>()};
            break;
        case CommandType::PromptsImport:
        case CommandType::MapExport:
            request.payload = PathPayload{payload.at("path").get<std::string>()};
            break;
        case CommandType::ChunkImport:
        case CommandType::ChunkWrite:
            request.payload = ChunkImagePayload{
                parse_coord(payload.at("coord")), payload.at("image").get<std::string>()};
            break;
        case CommandType::SeamInspect:
            request.payload = SeamInspectPayload{
                parse_coord(payload.at("coord")),
                payload.at("direction").get<std::string>() == "right"
                    ? CommandSeamDirection::Right : CommandSeamDirection::Bottom};
            break;
        case CommandType::PromptShow:
        case CommandType::ChunkContext:
        case CommandType::ChunkShow:
        case CommandType::ChunkRemove:
            request.payload = CoordPayload{parse_coord(payload.at("coord"))};
            break;
        case CommandType::ProjectOpen:
        case CommandType::ProjectStatus:
        case CommandType::ProjectValidate:
        case CommandType::ConceptContext:
        case CommandType::Render:
            request.payload = std::monostate{};
            break;
        }
        return Result<CommandRequest>::success(std::move(request));
    } catch (const std::exception& exception) {
        return Result<CommandRequest>::failure(
            "invalid_ipc_request", std::string("Invalid IPC request: ") + exception.what());
    }
}

json command_envelope(const CommandRequest& request,
                      const Result<CommandResult>& result) {
    const json project = request.project_name ? json(*request.project_name) :
        (request.type == CommandType::ProjectCreate
            ? json(std::get<ProjectCreatePayload>(request.payload).name) : json(nullptr));
    if (result) {
        return {{"schema_version", 1}, {"ok", true},
                {"command", command_name(request.type)}, {"project", project},
                {"data", result.value().data}};
    }
    return {{"schema_version", 1}, {"ok", false},
            {"command", command_name(request.type)}, {"project", project},
            {"error", {{"code", result.error().code},
                       {"message", result.error().message}}}};
}

json encode_ipc_reply(const CommandRequest& request,
                      const Result<CommandResult>& result) {
    return {
        {"protocol_version", 1},
        {"request_id", request.request_id},
        {"result", command_envelope(request, result)},
        {"text", result ? result.value().text : result.error().message},
        {"changes", result ? changes_json(result.value().changes) : json::object()},
    };
}

Result<IpcReply> decode_ipc_reply(const json& value) {
    try {
        if (value.at("protocol_version").get<int>() != 1) {
            return Result<IpcReply>::failure(
                "unsupported_ipc_protocol", "Only IPC protocol version 1 is supported.");
        }
        IpcReply reply;
        reply.envelope = value.at("result");
        reply.text = value.at("text").get<std::string>();
        return Result<IpcReply>::success(std::move(reply));
    } catch (const std::exception& exception) {
        return Result<IpcReply>::failure(
            "invalid_ipc_response", std::string("Invalid IPC response: ") + exception.what());
    }
}

}  // namespace chunkmap
