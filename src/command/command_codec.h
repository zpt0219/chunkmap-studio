#pragma once

#include "command/command_request.h"
#include "command/command_result.h"
#include "core/result.h"

#include <nlohmann/json.hpp>

#include <string>

namespace chunkmap {

struct IpcReply {
    nlohmann::json envelope;
    std::string text;
};

nlohmann::json encode_command_request(const CommandRequest& request);
Result<CommandRequest> decode_command_request(const nlohmann::json& value);
nlohmann::json encode_ipc_reply(const CommandRequest& request,
                                const Result<CommandResult>& result);
Result<IpcReply> decode_ipc_reply(const nlohmann::json& value);
nlohmann::json command_envelope(const CommandRequest& request,
                                const Result<CommandResult>& result);

}  // namespace chunkmap
