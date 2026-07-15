#include "image/layout_renderer.h"

#include "image/image_pipeline.h"

#include <algorithm>
#include <cmath>

namespace chunkmap {

namespace {

const std::uint8_t* sample(const ImageBuffer& image,
                           int x,
                           int y,
                           ChunkPlacement placement) {
    return image.pixel(
        std::clamp(x - placement.offset_x, 0, image.width() - 1),
        std::clamp(y - placement.offset_y, 0, image.height() - 1));
}

double seam_center(const SeamDefinition& seam, double along, int overlap) {
    const auto& points = seam.points;
    auto upper = std::upper_bound(
        points.begin(), points.end(), along,
        [](double value, const SeamPoint& point) { return value < point.along; });
    if (upper == points.begin()) return upper->across * static_cast<double>(overlap - 1);
    if (upper == points.end()) return points.back().across * static_cast<double>(overlap - 1);
    const auto& second = *upper;
    const auto& first = *(upper - 1);
    const double span = second.along - first.along;
    const double t = span > 0.0 ? (along - first.along) / span : 0.0;
    const double across = first.across + (second.across - first.across) * t;
    return across * static_cast<double>(overlap - 1);
}

void blend(const std::uint8_t* first,
           const std::uint8_t* second,
           double second_weight,
           std::uint8_t* destination) {
    for (int channel = 0; channel < 4; ++channel) {
        destination[channel] = static_cast<std::uint8_t>(std::lround(
            static_cast<double>(first[channel]) * (1.0 - second_weight) +
            static_cast<double>(second[channel]) * second_weight));
    }
}

Result<void> validate_image(const ImageBuffer& image,
                            const ProjectConfig& config) {
    if (!config.has_chunk_size() || image.width() != *config.chunk_width ||
        image.height() != *config.chunk_height) {
        return Result<void>::failure(
            "layout_size_mismatch", "Layout rendering requires exact-size Chunk images.");
    }
    return Result<void>::success();
}

}  // namespace

SeamDefinition LayoutRenderer::default_seam(const ProjectConfig& config, SeamKey key) {
    SeamDefinition seam;
    seam.key = key;
    const int dimension = key.direction == SeamDirection::Right
        ? config.chunk_width.value_or(0) : config.chunk_height.value_or(0);
    auto geometry = image_geometry(config);
    const int overlap = geometry
        ? (key.direction == SeamDirection::Right
               ? geometry.value().overlap_x : geometry.value().overlap_y)
        : 0;
    seam.feather_width = overlap > 0
        ? std::clamp(static_cast<int>(std::lround(dimension * 0.03)), 1, overlap)
        : 0;
    seam.points = {{0.0, 0.5}, {1.0, 0.5}};
    return seam;
}

Result<void> LayoutRenderer::validate_seam(const ProjectConfig& config,
                                           const SeamDefinition& seam) {
    auto geometry = image_geometry(config);
    if (!geometry) return Result<void>::failure(geometry.error().code, geometry.error().message);
    const int overlap = seam.key.direction == SeamDirection::Right
        ? geometry.value().overlap_x : geometry.value().overlap_y;
    if (!config.contains(seam.key.first) || !config.contains(seam_second(seam.key))) {
        return Result<void>::failure("seam_out_of_range", "Seam pair is outside the project grid.");
    }
    if (seam.feather_width < 0 || seam.feather_width > overlap) {
        return Result<void>::failure(
            "invalid_feather_width", "Seam feather width must fit within the overlap.");
    }
    if (seam.points.size() < 2U || seam.points.front().along != 0.0 ||
        seam.points.back().along != 1.0) {
        return Result<void>::failure(
            "invalid_seam_points", "Seam points must start at 0 and end at 1.");
    }
    double previous = -1.0;
    for (const auto& point : seam.points) {
        if (!std::isfinite(point.along) || !std::isfinite(point.across) ||
            point.along < 0.0 || point.along > 1.0 ||
            point.across < 0.0 || point.across > 1.0 ||
            point.along <= previous) {
            return Result<void>::failure(
                "invalid_seam_points", "Seam points must be ordered inside the overlap.");
        }
        previous = point.along;
    }
    return Result<void>::success();
}

Result<ImageBuffer> LayoutRenderer::render_placed_chunk(
    const ImageBuffer& source,
    const ProjectConfig& config,
    ChunkPlacement placement) {
    auto valid = validate_image(source, config);
    if (!valid) return Result<ImageBuffer>::failure(valid.error().code, valid.error().message);
    ImageBuffer result(source.width(), source.height());
    for (int y = 0; y < result.height(); ++y) {
        for (int x = 0; x < result.width(); ++x) {
            std::copy_n(sample(source, x, y, placement), 4, result.pixel(x, y));
        }
    }
    return Result<ImageBuffer>::success(std::move(result));
}

Result<ImageBuffer> LayoutRenderer::render_seam_patch(
    const ImageBuffer& first,
    const ImageBuffer& second,
    const ProjectConfig& config,
    ChunkPlacement first_placement,
    ChunkPlacement second_placement,
    const SeamDefinition& seam) {
    auto first_valid = validate_image(first, config);
    if (!first_valid) return Result<ImageBuffer>::failure(
        first_valid.error().code, first_valid.error().message);
    auto second_valid = validate_image(second, config);
    if (!second_valid) return Result<ImageBuffer>::failure(
        second_valid.error().code, second_valid.error().message);
    auto seam_valid = validate_seam(config, seam);
    if (!seam_valid) return Result<ImageBuffer>::failure(
        seam_valid.error().code, seam_valid.error().message);
    auto geometry = image_geometry(config);
    if (!geometry) return Result<ImageBuffer>::failure(
        geometry.error().code, geometry.error().message);

    const bool right = seam.key.direction == SeamDirection::Right;
    const int overlap = right ? geometry.value().overlap_x : geometry.value().overlap_y;
    const int patch_width = right ? overlap : *config.chunk_width;
    const int patch_height = right ? *config.chunk_height : overlap;
    ImageBuffer result(patch_width, patch_height);
    for (int y = 0; y < patch_height; ++y) {
        for (int x = 0; x < patch_width; ++x) {
            const int across = right ? x : y;
            const double along = right
                ? static_cast<double>(y) / std::max(1, patch_height - 1)
                : static_cast<double>(x) / std::max(1, patch_width - 1);
            const double center = seam_center(seam, along, overlap);
            double second_weight = across >= center ? 1.0 : 0.0;
            if (seam.feather_width > 0) {
                second_weight = std::clamp(
                    0.5 + (static_cast<double>(across) - center) /
                              static_cast<double>(seam.feather_width),
                    0.0, 1.0);
            }
            const int first_x = right ? *config.chunk_width - overlap + x : x;
            const int first_y = right ? y : *config.chunk_height - overlap + y;
            const int second_x = x;
            const int second_y = y;
            blend(sample(first, first_x, first_y, first_placement),
                  sample(second, second_x, second_y, second_placement),
                  second_weight, result.pixel(x, y));
        }
    }
    return Result<ImageBuffer>::success(std::move(result));
}

}  // namespace chunkmap
