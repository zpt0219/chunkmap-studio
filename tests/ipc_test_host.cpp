#include "command/document_command_queue.h"
#include "ipc/desktop_ipc.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32
std::string quote(const std::string& value) {
    std::string result = "\"";
    for (const char character : value) {
        if (character == '\"') result += '\\';
        result += character;
    }
    result += '\"';
    return result;
}

int run_child(int argc, char** argv) {
    std::string command;
    for (int index = 1; index < argc; ++index) {
        if (!command.empty()) command += ' ';
        command += quote(argv[index]);
    }
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<char> mutable_command(command.begin(), command.end());
    mutable_command.push_back('\0');
    if (!CreateProcessA(nullptr, mutable_command.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &startup, &process)) {
        return EXIT_FAILURE;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = EXIT_FAILURE;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exit_code);
}
#else
int run_child(int, char** argv) {
    const pid_t child = fork();
    if (child == 0) {
        execvp(argv[1], argv + 1);
        _exit(127);
    }
    if (child < 0) return EXIT_FAILURE;
    int status = 0;
    if (waitpid(child, &status, 0) < 0) return EXIT_FAILURE;
    return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "ipc_test_host requires a child command.\n";
        return EXIT_FAILURE;
    }
    chunkmap::DocumentCommandQueue queue;
    chunkmap::DesktopIpcServer server;
    auto started = server.start(queue);
    if (!started) {
        std::cerr << started.error().message << '\n';
        return EXIT_FAILURE;
    }
    const int result = run_child(argc, argv);
    server.stop();
    queue.stop_and_drain();
    return result;
}
