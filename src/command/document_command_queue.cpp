#include "command/document_command_queue.h"

#include <utility>

namespace chunkmap {

DocumentCommandQueue::DocumentCommandQueue()
    : worker_(&DocumentCommandQueue::worker_loop, this) {}

DocumentCommandQueue::~DocumentCommandQueue() {
    stop_and_drain();
}

std::future<Result<CommandResult>> DocumentCommandQueue::submit(CommandRequest request) {
    PendingCommand command;
    command.request = std::move(request);
    auto future = command.promise.get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_) {
            command.promise.set_value(Result<CommandResult>::failure(
                "command_queue_stopped", "Document command queue is shutting down."));
            return future;
        }
        pending_.push_back(std::move(command));
    }
    condition_.notify_one();
    return future;
}

std::vector<CommandCompletion> DocumentCommandQueue::take_completions() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CommandCompletion> result;
    result.swap(completed_);
    return result;
}

void DocumentCommandQueue::stop_and_drain() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        accepting_ = false;
        stopping_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void DocumentCommandQueue::worker_loop() {
    while (true) {
        PendingCommand command;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [&] { return stopping_ || !pending_.empty(); });
            if (pending_.empty()) {
                if (stopping_) break;
                continue;
            }
            command = std::move(pending_.front());
            pending_.pop_front();
        }

        auto result = dispatcher_.execute(command.request);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            completed_.push_back({command.request, result});
        }
        command.promise.set_value(std::move(result));
    }
}

}  // namespace chunkmap
