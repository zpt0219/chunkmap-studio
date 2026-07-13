#include "desktop_command_host.h"

#include <stdexcept>
#include <utility>

namespace chunkmap_desktop {

DesktopCommandHost::DesktopCommandHost() {
    auto started = server_.start(queue_);
    if (!started) throw std::runtime_error(started.error().message);
}

DesktopCommandHost::~DesktopCommandHost() {
    server_.stop();
    queue_.stop_and_drain();
}

std::future<chunkmap::Result<chunkmap::CommandResult>> DesktopCommandHost::submit(
    chunkmap::CommandRequest request) {
    return queue_.submit(std::move(request));
}

chunkmap::Result<chunkmap::CommandResult> DesktopCommandHost::submit_and_wait(
    chunkmap::CommandRequest request) {
    return submit(std::move(request)).get();
}

chunkmap::CommandQueueUpdates DesktopCommandHost::take_updates() {
    return queue_.take_updates();
}

}  // namespace chunkmap_desktop
