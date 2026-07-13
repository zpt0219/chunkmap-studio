#pragma once

#include "command/command_dispatcher.h"

#include <condition_variable>
#include <chrono>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace chunkmap {

struct CommandCompletion {
    CommandRequest request;
    Result<CommandResult> result;
    double queue_wait_ms = 0.0;
    double execution_ms = 0.0;
};

struct CommandProgress {
    std::string request_id;
    CommandType type = CommandType::ProjectStatus;
    std::size_t completed = 0;
    std::size_t total = 0;
    std::string message;
};

struct CommandQueueUpdates {
    std::vector<CommandProgress> progress;
    std::vector<CommandCompletion> completions;
};

class DocumentCommandQueue {
public:
    DocumentCommandQueue();
    ~DocumentCommandQueue();
    DocumentCommandQueue(const DocumentCommandQueue&) = delete;
    DocumentCommandQueue& operator=(const DocumentCommandQueue&) = delete;

    std::future<Result<CommandResult>> submit(CommandRequest request);
    CommandQueueUpdates take_updates();
    std::vector<CommandCompletion> take_completions();
    void stop_and_drain();

private:
    struct PendingCommand {
        CommandRequest request;
        std::promise<Result<CommandResult>> promise;
        std::chrono::steady_clock::time_point enqueued_at;
    };

    void worker_loop();

    CommandDispatcher dispatcher_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<PendingCommand> pending_;
    std::vector<CommandProgress> progress_;
    std::vector<CommandCompletion> completed_;
    bool accepting_ = true;
    bool stopping_ = false;
    std::thread worker_;
};

}  // namespace chunkmap
