#pragma once

#include "command/command_request.h"
#include "command/command_result.h"
#include "core/result.h"

namespace chunkmap {

class DocumentCommandQueue;

class CommandDispatcher {
    friend class DocumentCommandQueue;

private:
    Result<CommandResult> execute(const CommandRequest& request) const;
};

}  // namespace chunkmap
