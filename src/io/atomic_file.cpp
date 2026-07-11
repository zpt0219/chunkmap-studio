#include "io/atomic_file.h"

#include <fstream>
#include <limits>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace chunkmap::atomic_file {

namespace {

Result<void> replace_with_temp(const std::filesystem::path& temp,
                               const std::filesystem::path& target) {
#ifdef _WIN32
    if (MoveFileExW(temp.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        return Result<void>::success();
    }
    const DWORD windows_error = GetLastError();
    std::error_code cleanup_error;
    std::filesystem::remove(temp, cleanup_error);
    return Result<void>::failure(
        "atomic_rename_failed",
        "Unable to replace file: " + target.string() +
            " (Windows error " + std::to_string(windows_error) + ")");
#else
    std::error_code error;
    std::filesystem::rename(temp, target, error);
    if (!error) return Result<void>::success();
    std::filesystem::remove(temp, error);
    return Result<void>::failure(
        "atomic_rename_failed",
        "Unable to replace file: " + target.string());
#endif
}

Result<void> ensure_parent(const std::filesystem::path& path) {
    const auto parent = path.parent_path();
    if (parent.empty()) return Result<void>::success();

    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error) {
        return Result<void>::failure(
            "create_directory_failed",
            "Unable to create directory: " + parent.string());
    }
    return Result<void>::success();
}

}  // namespace

Result<void> write_text(const std::filesystem::path& path, std::string_view content) {
    auto parent_result = ensure_parent(path);
    if (!parent_result) return parent_result;

    const auto temp = path.string() + ".tmp";
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) {
            return Result<void>::failure(
                "file_open_failed", "Unable to open file for writing: " + temp);
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.close();
        if (!output) {
            std::error_code error;
            std::filesystem::remove(temp, error);
            return Result<void>::failure(
                "file_write_failed", "Unable to write file: " + temp);
        }
    }
    return replace_with_temp(temp, path);
}

Result<void> write_binary(const std::filesystem::path& path,
                          const std::vector<std::uint8_t>& content) {
    if (content.empty()) return write_text(path, {});
    return write_text(path, std::string_view(
        reinterpret_cast<const char*>(content.data()), content.size()));
}

Result<std::string> read_text(const std::filesystem::path& path) {
    auto binary = read_binary(path);
    if (!binary) return Result<std::string>::failure(binary.error().code, binary.error().message);
    const auto& data = binary.value();
    if (data.empty()) return Result<std::string>::success({});
    return Result<std::string>::success(
        std::string(reinterpret_cast<const char*>(data.data()), data.size()));
}

Result<std::vector<std::uint8_t>> read_binary(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Result<std::vector<std::uint8_t>>::failure(
            "file_open_failed", "Unable to open file: " + path.string());
    }

    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0) {
        return Result<std::vector<std::uint8_t>>::failure(
            "file_read_failed", "Unable to read file size: " + path.string());
    }
    const auto unsigned_length = static_cast<std::uintmax_t>(length);
    if (unsigned_length > std::numeric_limits<std::size_t>::max() ||
        unsigned_length > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        return Result<std::vector<std::uint8_t>>::failure(
            "file_too_large", "File is too large to read safely: " + path.string());
    }
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> data(static_cast<std::size_t>(unsigned_length));
    if (!data.empty()) {
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(unsigned_length));
    }
    if (!input) {
        return Result<std::vector<std::uint8_t>>::failure(
            "file_read_failed", "Unable to read file: " + path.string());
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(data));
}

}  // namespace chunkmap::atomic_file
