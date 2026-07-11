#pragma once

#include "core/result.h"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace chunkmap::atomic_file {

Result<void> write_text(const std::filesystem::path& path, std::string_view content);
Result<void> write_binary(const std::filesystem::path& path,
                          const std::vector<std::uint8_t>& content);
Result<std::string> read_text(const std::filesystem::path& path);
Result<std::vector<std::uint8_t>> read_binary(const std::filesystem::path& path);

}  // namespace chunkmap::atomic_file
