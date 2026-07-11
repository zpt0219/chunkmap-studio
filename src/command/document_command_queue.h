#pragma once

#include "command/command_dispatcher.h"

#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace chunkmap {

struct CommandCompletion {
    CommandRequest request;
    Result<CommandResult> result;
};

class DocumentCommandQueue {
public:
    DocumentCommandQueue();
    ~DocumentCommandQueue();
    DocumentCommandQueue(const DocumentCommandQueue&) = delete;
    DocumentCommandQueue& operator=(const DocumentCommandQueue&) = delete;

    std::future<Result<CommandResult>> submit(CommandRequest request);
    std::vector<CommandCompletion> take_completions();
    void stop_and_drain();

private:
    struct PendingCommand {
        CommandRequest request;
        std::promise<Result<CommandResult>> promise;
    };

    void worker_loop();

    CommandDispatcher dispatcher_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<PendingCommand> pending_;
    std::vector<CommandCompletion> completed_;
    bool accepting_ = true;
    bool stopping_ = false;
    std::thread worker_;
};

}  // namespace chunkmap
