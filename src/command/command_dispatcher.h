#pragma once

#include "command/command_request.h"
#include "command/command_result.h"
#include "core/result.h"
#include "project/project_session.h"

namespace chunkmap {

class DocumentCommandQueue;

class CommandDispatcher {
    friend class DocumentCommandQueue;

private:
    Result<CommandResult> execute(const CommandRequest& request);

    ProjectSession session_;
};

}  // namespace chunkmap
