#pragma once

#include "core/result.h"
#include "model/project_document.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

namespace chunkmap {

struct FullMapExportOptions {
    std::filesystem::path output;
    bool overwrite = false;
    std::size_t maximum_band_bytes = 32U * 1024U * 1024U;
};

struct FullMapExportResult {
    std::filesystem::path output;
    int width = 0;
    int height = 0;
    int ready_chunks = 0;
    int empty_chunks = 0;
};

using FullMapExportProgress =
    std::function<void(std::size_t completed, std::size_t total, const std::string& message)>;

Result<FullMapExportResult> export_full_map(
    ProjectDocument& document, const FullMapExportOptions& options,
    const FullMapExportProgress& progress = {});

}  // namespace chunkmap
