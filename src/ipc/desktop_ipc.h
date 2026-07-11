#pragma once

#include "command/command_codec.h"
#include "command/document_command_queue.h"
#include "core/result.h"

#include <atomic>
#include <filesystem>
#include <thread>

namespace chunkmap {

std::filesystem::path desktop_ipc_endpoint();

class DesktopIpcClient {
public:
    Result<IpcReply> send(const CommandRequest& request) const;
};

class DesktopIpcServer {
public:
    DesktopIpcServer() = default;
    ~DesktopIpcServer();
    DesktopIpcServer(const DesktopIpcServer&) = delete;
    DesktopIpcServer& operator=(const DesktopIpcServer&) = delete;

    Result<void> start(DocumentCommandQueue& queue);
    void stop();
    bool running() const { return running_; }

private:
    void server_loop();

    DocumentCommandQueue* queue_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread thread_;
#ifdef _WIN32
    void* pipe_ = nullptr;
    void* instance_mutex_ = nullptr;
#else
    int listen_fd_ = -1;
#endif
};

}  // namespace chunkmap
