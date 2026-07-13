#pragma once

#include "command/command_request.h"
#include "command/command_result.h"
#include "core/result.h"
#include "project/project_session.h"

#include <cstddef>
#include <functional>
#include <string>

namespace chunkmap {

class DocumentCommandQueue;

using CommandProgressCallback =
    std::function<void(std::size_t completed, std::size_t total, const std::string& message)>;

class CommandDispatcher {
    friend class DocumentCommandQueue;

private:
    Result<CommandResult> execute(
        const CommandRequest& request, const CommandProgressCallback& progress = {});

    ProjectSession session_;
};

}  // namespace chunkmap
