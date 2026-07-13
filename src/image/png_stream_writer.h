#pragma once

#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace chunkmap {

class PngStreamWriter {
public:
    PngStreamWriter();
    ~PngStreamWriter();
    PngStreamWriter(const PngStreamWriter&) = delete;
    PngStreamWriter& operator=(const PngStreamWriter&) = delete;

    Result<void> open(const std::filesystem::path& path, int width, int height);
    Result<void> write_rows(const std::uint8_t* rgba, int row_count, std::size_t stride);
    Result<void> finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace chunkmap
