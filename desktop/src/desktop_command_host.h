#pragma once

#include "command/document_command_queue.h"
#include "ipc/desktop_ipc.h"

#include <future>
#include <vector>

namespace chunkmap_desktop {

class DesktopCommandHost {
public:
    DesktopCommandHost();
    ~DesktopCommandHost();
    DesktopCommandHost(const DesktopCommandHost&) = delete;
    DesktopCommandHost& operator=(const DesktopCommandHost&) = delete;

    std::future<chunkmap::Result<chunkmap::CommandResult>> submit(
        chunkmap::CommandRequest request);
    chunkmap::Result<chunkmap::CommandResult> submit_and_wait(
        chunkmap::CommandRequest request);
    std::vector<chunkmap::CommandCompletion> take_completions();

private:
    chunkmap::DocumentCommandQueue queue_;
    chunkmap::DesktopIpcServer server_;
};

}  // namespace chunkmap_desktop
