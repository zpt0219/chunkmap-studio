#include "image/image_registration.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace chunkmap {

namespace {

constexpr int kMaximumShift = 12;
constexpr int kSampleStep = 2;
constexpr double kColorWeight = 0.15;
constexpr double kShiftPenalty = 0.02;
constexpr double kMinimumRelativeImprovement = 0.0075;

std::uint8_t luminance(const std::uint8_t* pixel) {
    return static_cast<std::uint8_t>(
        (77U * pixel[0] + 150U * pixel[1] + 29U * pixel[2]) >> 8U);
}

int gradient(const ImageBuffer& image, int x, int y) {
    const int left = std::max(0, x - 1);
    const int right = std::min(image.width() - 1, x + 1);
    const int top = std::max(0, y - 1);
    const int bottom = std::min(image.height() - 1, y + 1);
    const int horizontal = std::abs(
        static_cast<int>(luminance(image.pixel(right, y))) -
        static_cast<int>(luminance(image.pixel(left, y))));
    const int vertical = std::abs(
        static_cast<int>(luminance(image.pixel(x, bottom))) -
        static_cast<int>(luminance(image.pixel(x, top))));
    return horizontal + vertical;
}

struct Comparison {
    const ImageBuffer* neighbor = nullptr;
    Rect candidate_rect;
    Rect neighbor_rect;
};

std::vector<Comparison> comparisons(const ProjectConfig& config,
                                    const NeighborImages& neighbors,
                                    const ImageGeometry& geometry) {
    const int width = *config.chunk_width;
    const int height = *config.chunk_height;
    std::vector<Comparison> result;
    if (neighbors.left) {
        result.push_back({&*neighbors.left,
                          {0, 0, geometry.overlap_x, height},
                          {width - geometry.overlap_x, 0, geometry.overlap_x, height}});
    }
    if (neighbors.right) {
        result.push_back({&*neighbors.right,
                          {width - geometry.overlap_x, 0, geometry.overlap_x, height},
                          {0, 0, geometry.overlap_x, height}});
    }
    if (neighbors.top) {
        result.push_back({&*neighbors.top,
                          {0, 0, width, geometry.overlap_y},
                          {0, height - geometry.overlap_y, width, geometry.overlap_y}});
    }
    if (neighbors.bottom) {
        result.push_back({&*neighbors.bottom,
                          {0, height - geometry.overlap_y, width, geometry.overlap_y},
                          {0, 0, width, geometry.overlap_y}});
    }
    return result;
}

double alignment_score(const ImageBuffer& source,
                       const std::vector<Comparison>& regions,
                       int offset_x,
                       int offset_y) {
    double structural_error = 0.0;
    double color_error = 0.0;
    std::uint64_t samples = 0;
    for (const auto& region : regions) {
        for (int y = 0; y < region.candidate_rect.height; y += kSampleStep) {
            for (int x = 0; x < region.candidate_rect.width; x += kSampleStep) {
                const int output_x = region.candidate_rect.x + x;
                const int output_y = region.candidate_rect.y + y;
                const int source_x = std::clamp(output_x - offset_x, 0, source.width() - 1);
                const int source_y = std::clamp(output_y - offset_y, 0, source.height() - 1);
                const int neighbor_x = region.neighbor_rect.x + x;
                const int neighbor_y = region.neighbor_rect.y + y;
                structural_error += std::abs(
                    gradient(source, source_x, source_y) -
                    gradient(*region.neighbor, neighbor_x, neighbor_y));
                const auto* candidate_pixel = source.pixel(source_x, source_y);
                const auto* neighbor_pixel = region.neighbor->pixel(neighbor_x, neighbor_y);
                for (int channel = 0; channel < 3; ++channel) {
                    color_error += std::abs(
                        static_cast<int>(candidate_pixel[channel]) -
                        static_cast<int>(neighbor_pixel[channel]));
                }
                ++samples;
            }
        }
    }
    if (samples == 0) return std::numeric_limits<double>::infinity();
    const double structure = structural_error / static_cast<double>(samples);
    const double color = color_error / static_cast<double>(samples * 3U);
    const double penalty = kShiftPenalty * (std::abs(offset_x) + std::abs(offset_y));
    return structure + kColorWeight * color + penalty;
}

ImageBuffer shifted_image(const ImageBuffer& source, int offset_x, int offset_y) {
    ImageBuffer result(source.width(), source.height());
    for (int y = 0; y < source.height(); ++y) {
        const int source_y = std::clamp(y - offset_y, 0, source.height() - 1);
        for (int x = 0; x < source.width(); ++x) {
            const int source_x = std::clamp(x - offset_x, 0, source.width() - 1);
            std::copy_n(source.pixel(source_x, source_y), 4, result.pixel(x, y));
        }
    }
    return result;
}

struct Candidate {
    int x = 0;
    int y = 0;
    double score = std::numeric_limits<double>::infinity();
};

bool better_candidate(const Candidate& candidate, const Candidate& current) {
    if (candidate.score != current.score) return candidate.score < current.score;
    const int candidate_distance = std::abs(candidate.x) + std::abs(candidate.y);
    const int current_distance = std::abs(current.x) + std::abs(current.y);
    if (candidate_distance != current_distance) return candidate_distance < current_distance;
    if (candidate.y != current.y) return candidate.y < current.y;
    return candidate.x < current.x;
}

}  // namespace

Result<ImageRegistrationResult> ImageRegistration::align(
    const ImageBuffer& source,
    const ProjectConfig& config,
    const NeighborImages& neighbors) {
    auto geometry = image_geometry(config);
    if (!geometry) {
        return Result<ImageRegistrationResult>::failure(
            geometry.error().code, geometry.error().message);
    }
    if (source.width() != *config.chunk_width ||
        source.height() != *config.chunk_height) {
        return Result<ImageRegistrationResult>::failure(
            "registration_size_mismatch",
            "Image registration requires a normalized chunk image.");
    }
    for (const auto* neighbor : {
             &neighbors.top, &neighbors.bottom, &neighbors.left, &neighbors.right}) {
        if (*neighbor && ((*neighbor)->width() != *config.chunk_width ||
                          (*neighbor)->height() != *config.chunk_height)) {
            return Result<ImageRegistrationResult>::failure(
                "neighbor_size_mismatch",
                "A neighbor image does not match the project chunk size.");
        }
    }

    ImageRegistrationResult result;
    result.image = source;
    if (neighbors.count() == 0) {
        return Result<ImageRegistrationResult>::success(std::move(result));
    }
    const auto regions = comparisons(config, neighbors, geometry.value());
    result.score_before = alignment_score(source, regions, 0, 0);
    result.score_after = result.score_before;

    const int maximum_x = std::min(
        kMaximumShift, std::max(1, geometry.value().overlap_x / 16));
    const int maximum_y = std::min(
        kMaximumShift, std::max(1, geometry.value().overlap_y / 16));
    Candidate best{0, 0, result.score_before};
    for (int y = -maximum_y; y <= maximum_y; y += 2) {
        for (int x = -maximum_x; x <= maximum_x; x += 2) {
            Candidate candidate{x, y, alignment_score(source, regions, x, y)};
            if (better_candidate(candidate, best)) best = candidate;
        }
    }
    const int coarse_x = best.x;
    const int coarse_y = best.y;
    for (int y = std::max(-maximum_y, coarse_y - 1);
         y <= std::min(maximum_y, coarse_y + 1); ++y) {
        for (int x = std::max(-maximum_x, coarse_x - 1);
             x <= std::min(maximum_x, coarse_x + 1); ++x) {
            Candidate candidate{x, y, alignment_score(source, regions, x, y)};
            if (better_candidate(candidate, best)) best = candidate;
        }
    }

    const double improvement = result.score_before > 0.0
        ? (result.score_before - best.score) / result.score_before : 0.0;
    const bool at_search_boundary =
        std::abs(best.x) == maximum_x || std::abs(best.y) == maximum_y;
    if ((best.x != 0 || best.y != 0) && !at_search_boundary &&
        improvement >= kMinimumRelativeImprovement) {
        result.image = shifted_image(source, best.x, best.y);
        result.offset_x = best.x;
        result.offset_y = best.y;
        result.score_after = best.score;
        result.applied = true;
    }
    return Result<ImageRegistrationResult>::success(std::move(result));
}

}  // namespace chunkmap
