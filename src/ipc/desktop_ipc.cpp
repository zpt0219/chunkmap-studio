#include "ipc/desktop_ipc.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace chunkmap {

namespace {

constexpr std::uint32_t kMaximumMessageSize = 64U * 1024U * 1024U;

std::array<std::uint8_t, 4> encode_length(std::uint32_t value) {
    return {static_cast<std::uint8_t>(value & 0xffU),
            static_cast<std::uint8_t>((value >> 8U) & 0xffU),
            static_cast<std::uint8_t>((value >> 16U) & 0xffU),
            static_cast<std::uint8_t>((value >> 24U) & 0xffU)};
}

std::uint32_t decode_length(const std::array<std::uint8_t, 4>& bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

#ifdef _WIN32

std::wstring pipe_name() {
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    return L"\\\\.\\pipe\\chunkmap-studio-" + std::to_wstring(session);
}

std::wstring mutex_name() {
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    return L"Local\\chunkmap-studio-" + std::to_wstring(session);
}

bool read_exact(HANDLE handle, void* output, std::size_t size) {
    auto* bytes = static_cast<std::uint8_t*>(output);
    while (size > 0) {
        DWORD read = 0;
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
            size, std::numeric_limits<DWORD>::max()));
        if (!ReadFile(handle, bytes, request, &read, nullptr) || read == 0) return false;
        bytes += read;
        size -= read;
    }
    return true;
}

bool write_exact(HANDLE handle, const void* input, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(input);
    while (size > 0) {
        DWORD written = 0;
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
            size, std::numeric_limits<DWORD>::max()));
        if (!WriteFile(handle, bytes, request, &written, nullptr) || written == 0) return false;
        bytes += written;
        size -= written;
    }
    return true;
}

Result<std::string> receive_message(HANDLE handle) {
    std::array<std::uint8_t, 4> header{};
    if (!read_exact(handle, header.data(), header.size())) {
        return Result<std::string>::failure("ipc_read_failed", "Unable to read IPC message header.");
    }
    const auto size = decode_length(header);
    if (size == 0 || size > kMaximumMessageSize) {
        return Result<std::string>::failure("ipc_message_size", "IPC message size is invalid.");
    }
    std::string message(size, '\0');
    if (!read_exact(handle, message.data(), message.size())) {
        return Result<std::string>::failure("ipc_read_failed", "Unable to read IPC message body.");
    }
    return Result<std::string>::success(std::move(message));
}

bool send_message(HANDLE handle, const std::string& message) {
    if (message.empty() || message.size() > kMaximumMessageSize) return false;
    const auto header = encode_length(static_cast<std::uint32_t>(message.size()));
    return write_exact(handle, header.data(), header.size()) &&
           write_exact(handle, message.data(), message.size());
}

#else

bool read_exact(int fd, void* output, std::size_t size) {
    auto* bytes = static_cast<std::uint8_t*>(output);
    while (size > 0) {
        const auto count = ::recv(fd, bytes, size, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        bytes += count;
        size -= static_cast<std::size_t>(count);
    }
    return true;
}

bool write_exact(int fd, const void* input, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(input);
    while (size > 0) {
#ifdef MSG_NOSIGNAL
        const auto count = ::send(fd, bytes, size, MSG_NOSIGNAL);
#else
        const auto count = ::send(fd, bytes, size, 0);
#endif
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        bytes += count;
        size -= static_cast<std::size_t>(count);
    }
    return true;
}

Result<std::string> receive_message(int fd) {
    std::array<std::uint8_t, 4> header{};
    if (!read_exact(fd, header.data(), header.size())) {
        return Result<std::string>::failure("ipc_read_failed", "Unable to read IPC message header.");
    }
    const auto size = decode_length(header);
    if (size == 0 || size > kMaximumMessageSize) {
        return Result<std::string>::failure("ipc_message_size", "IPC message size is invalid.");
    }
    std::string message(size, '\0');
    if (!read_exact(fd, message.data(), message.size())) {
        return Result<std::string>::failure("ipc_read_failed", "Unable to read IPC message body.");
    }
    return Result<std::string>::success(std::move(message));
}

bool send_message(int fd, const std::string& message) {
    if (message.empty() || message.size() > kMaximumMessageSize) return false;
    const auto header = encode_length(static_cast<std::uint32_t>(message.size()));
    return write_exact(fd, header.data(), header.size()) &&
           write_exact(fd, message.data(), message.size());
}

sockaddr_un socket_address(const std::filesystem::path& path) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto text = path.string();
    std::strncpy(address.sun_path, text.c_str(), sizeof(address.sun_path) - 1U);
    return address;
}

int connect_socket() {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    const auto address = socket_address(desktop_ipc_endpoint());
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

#endif

nlohmann::json malformed_reply(const std::string& request_id, const Error& error) {
    return {
        {"protocol_version", 1},
        {"request_id", request_id},
        {"result", {{"schema_version", 1}, {"ok", false},
                    {"command", "ipc"}, {"project", nullptr},
                    {"error", {{"code", error.code}, {"message", error.message}}}}},
        {"text", error.message},
    };
}

}  // namespace

std::filesystem::path desktop_ipc_endpoint() {
#ifdef _WIN32
    return std::filesystem::path(pipe_name());
#else
    std::error_code error;
    auto root = std::filesystem::temp_directory_path(error);
    if (error) root = "/tmp";
    return root / ("chunkmap-studio-" + std::to_string(static_cast<unsigned long>(::getuid())) + ".sock");
#endif
}

Result<IpcReply> DesktopIpcClient::send(const CommandRequest& request) const {
    const std::string encoded = encode_command_request(request).dump();
#ifdef _WIN32
    const auto name = pipe_name();
    if (!WaitNamedPipeW(name.c_str(), 500)) {
        return Result<IpcReply>::failure(
            "desktop_not_running", "AI Chunk Map Studio must be running before using chunkmap CLI.");
    }
    HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE,
                              0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        return Result<IpcReply>::failure(
            "desktop_not_running", "AI Chunk Map Studio must be running before using chunkmap CLI.");
    }
    if (!send_message(pipe, encoded)) {
        CloseHandle(pipe);
        return Result<IpcReply>::failure("ipc_write_failed", "Unable to send command to Desktop.");
    }
    auto response = receive_message(pipe);
    CloseHandle(pipe);
#else
    const int fd = connect_socket();
    if (fd < 0) {
        return Result<IpcReply>::failure(
            "desktop_not_running", "AI Chunk Map Studio must be running before using chunkmap CLI.");
    }
    if (!send_message(fd, encoded)) {
        ::close(fd);
        return Result<IpcReply>::failure("ipc_write_failed", "Unable to send command to Desktop.");
    }
    auto response = receive_message(fd);
    ::close(fd);
#endif
    if (!response) return Result<IpcReply>::failure(response.error().code, response.error().message);
    try {
        return decode_ipc_reply(nlohmann::json::parse(response.value()));
    } catch (const std::exception& exception) {
        return Result<IpcReply>::failure(
            "invalid_ipc_response", std::string("Unable to parse Desktop response: ") + exception.what());
    }
}

DesktopIpcServer::~DesktopIpcServer() {
    stop();
}

Result<void> DesktopIpcServer::start(DocumentCommandQueue& queue) {
    if (running_) return Result<void>::success();
    queue_ = &queue;
#ifdef _WIN32
    HANDLE instance_mutex = CreateMutexW(nullptr, TRUE, mutex_name().c_str());
    if (instance_mutex == nullptr) {
        return Result<void>::failure(
            "ipc_mutex_failed", "Unable to create the Desktop single-instance mutex.");
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(instance_mutex);
        return Result<void>::failure(
            "desktop_already_running", "AI Chunk Map Studio is already running.");
    }
    instance_mutex_ = instance_mutex;
#else
    const auto endpoint = desktop_ipc_endpoint();
    const auto endpoint_text = endpoint.string();
    if (endpoint_text.size() >= sizeof(sockaddr_un::sun_path)) {
        return Result<void>::failure("ipc_path_too_long", "Desktop IPC socket path is too long.");
    }
    if (std::filesystem::exists(endpoint)) {
        const int probe = connect_socket();
        if (probe >= 0) {
            ::close(probe);
            return Result<void>::failure(
                "desktop_already_running", "AI Chunk Map Studio is already running.");
        }
        std::error_code remove_error;
        std::filesystem::remove(endpoint, remove_error);
        if (remove_error) {
            return Result<void>::failure(
                "ipc_cleanup_failed", "Unable to remove stale Desktop IPC socket.");
        }
    }
    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        return Result<void>::failure("ipc_socket_failed", "Unable to create Desktop IPC socket.");
    }
    const auto address = socket_address(endpoint);
    if (::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::chmod(endpoint_text.c_str(), S_IRUSR | S_IWUSR) != 0 ||
        ::listen(listen_fd_, 8) != 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        std::error_code remove_error;
        std::filesystem::remove(endpoint, remove_error);
        return Result<void>::failure("ipc_bind_failed", "Unable to bind Desktop IPC socket.");
    }
#endif
    running_ = true;
    thread_ = std::thread(&DesktopIpcServer::server_loop, this);
    return Result<void>::success();
}

void DesktopIpcServer::stop() {
    if (!running_.exchange(false)) return;
#ifdef _WIN32
    const auto name = pipe_name();
    HANDLE wake = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE,
                              0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
#else
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
#endif
    if (thread_.joinable()) thread_.join();
#ifdef _WIN32
    if (instance_mutex_ != nullptr) {
        ReleaseMutex(static_cast<HANDLE>(instance_mutex_));
        CloseHandle(static_cast<HANDLE>(instance_mutex_));
        instance_mutex_ = nullptr;
    }
#endif
#ifndef _WIN32
    std::error_code error;
    std::filesystem::remove(desktop_ipc_endpoint(), error);
#endif
}

void DesktopIpcServer::server_loop() {
    while (running_) {
#ifdef _WIN32
        const auto name = pipe_name();
        HANDLE client = CreateNamedPipeW(
            name.c_str(), PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            8, 64 * 1024, 64 * 1024, 0, nullptr);
        if (client == INVALID_HANDLE_VALUE) break;
        pipe_ = client;
        const BOOL connected = ConnectNamedPipe(client, nullptr)
            ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected || !running_) {
            CloseHandle(client);
            pipe_ = nullptr;
            continue;
        }
#else
        const int client = ::accept(listen_fd_, nullptr, nullptr);
        if (client < 0) {
            if (!running_) break;
            if (errno == EINTR) continue;
            continue;
        }
#endif
        auto message = receive_message(client);
        nlohmann::json response;
        if (!message) {
            response = malformed_reply({}, message.error());
        } else {
            try {
                const auto parsed_json = nlohmann::json::parse(message.value());
                auto request = decode_command_request(parsed_json);
                if (!request) {
                    const auto request_id = parsed_json.value("request_id", std::string{});
                    response = malformed_reply(request_id, request.error());
                } else {
                    auto future = queue_->submit(request.value());
                    auto result = future.get();
                    response = encode_ipc_reply(request.value(), result);
                }
            } catch (const std::exception& exception) {
                response = malformed_reply({}, {
                    "invalid_ipc_request", std::string("Unable to parse IPC request: ") + exception.what()});
            }
        }
        send_message(client, response.dump());
#ifdef _WIN32
        FlushFileBuffers(client);
        DisconnectNamedPipe(client);
        CloseHandle(client);
        pipe_ = nullptr;
#else
        ::close(client);
#endif
    }
}

}  // namespace chunkmap
