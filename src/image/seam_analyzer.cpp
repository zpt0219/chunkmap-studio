#include "image/seam_analyzer.h"

#include "image/image_pipeline.h"

#include <cmath>
#include <cstdint>

namespace chunkmap {

Result<SeamAnalysis> SeamAnalyzer::analyze(const ImageBuffer& first,
                                           const ImageBuffer& second,
                                           const ProjectConfig& config,
                                           SeamDirection direction) {
    auto geometry = image_geometry(config);
    if (!geometry) return Result<SeamAnalysis>::failure(geometry.error().code, geometry.error().message);
    if (first.width() != *config.chunk_width || first.height() != *config.chunk_height ||
        second.width() != *config.chunk_width || second.height() != *config.chunk_height) {
        return Result<SeamAnalysis>::failure(
            "chunk_size_mismatch", "Seam images must match the project chunk size.");
    }

    const bool horizontal = direction == SeamDirection::Right;
    const int preview_width = horizontal ? geometry.value().overlap_x : *config.chunk_width;
    const int preview_height = horizontal ? *config.chunk_height : geometry.value().overlap_y;
    SeamAnalysis result;
    result.direction = direction;
    result.overlap_pixels = horizontal ? geometry.value().overlap_x : geometry.value().overlap_y;
    result.overlap_preview = ImageBuffer(preview_width, preview_height);
    result.difference_preview = ImageBuffer(preview_width, preview_height);

    std::uint64_t difference_sum = 0;
    std::uint64_t sample_count = 0;
    for (int y = 0; y < preview_height; ++y) {
        for (int x = 0; x < preview_width; ++x) {
            const int first_x = horizontal ? first.width() - preview_width + x : x;
            const int first_y = horizontal ? y : first.height() - preview_height + y;
            const auto* a = first.pixel(first_x, first_y);
            const auto* b = second.pixel(x, y);
            auto* overlap = result.overlap_preview.pixel(x, y);
            auto* difference = result.difference_preview.pixel(x, y);
            for (int channel = 0; channel < 3; ++channel) {
                const int delta = std::abs(static_cast<int>(a[channel]) - static_cast<int>(b[channel]));
                difference_sum += static_cast<std::uint64_t>(delta);
                ++sample_count;
                overlap[channel] = static_cast<std::uint8_t>(
                    (static_cast<int>(a[channel]) + static_cast<int>(b[channel])) / 2);
                difference[channel] = static_cast<std::uint8_t>(delta);
            }
            overlap[3] = 255;
            difference[3] = 255;
        }
    }
    result.mean_absolute_rgb_difference = sample_count == 0 ? 0.0 :
        static_cast<double>(difference_sum) / static_cast<double>(sample_count);
    return Result<SeamAnalysis>::success(std::move(result));
}

}  // namespace chunkmap
