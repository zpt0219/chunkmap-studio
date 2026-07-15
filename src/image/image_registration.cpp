#include "image/image_registration.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace chunkmap {

namespace {

constexpr int kMaximumHorizontalShift = 96;
constexpr int kMaximumVerticalShift = 64;
constexpr int kCoarseScale = 8;
constexpr int kRefineRadius = 4;
constexpr int kValidationSampleStep = 2;
constexpr double kShiftPenalty = 0.002;
constexpr double kMinimumRelativeImprovement = 0.02;

struct FeatureImage {
    int width = 0;
    int height = 0;
    std::vector<float> values;

    float at(int x, int y) const {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return values[static_cast<std::size_t>(y) * width + x];
    }
};

struct FeatureNeighbors {
    std::optional<FeatureImage> top;
    std::optional<FeatureImage> bottom;
    std::optional<FeatureImage> left;
    std::optional<FeatureImage> right;
};

struct FeatureComparison {
    const FeatureImage* neighbor = nullptr;
    Rect candidate_rect;
    Rect neighbor_rect;
};

struct OffsetScore {
    int x = 0;
    int y = 0;
    double score = std::numeric_limits<double>::infinity();
};

float luminance(const std::uint8_t* pixel) {
    return static_cast<float>(77U * pixel[0] + 150U * pixel[1] + 29U * pixel[2]) /
           256.0F;
}

FeatureImage feature_image(const ImageBuffer& image, int scale) {
    FeatureImage gray;
    gray.width = (image.width() + scale - 1) / scale;
    gray.height = (image.height() + scale - 1) / scale;
    gray.values.resize(static_cast<std::size_t>(gray.width) * gray.height);
    for (int y = 0; y < gray.height; ++y) {
        for (int x = 0; x < gray.width; ++x) {
            const int x0 = x * scale;
            const int y0 = y * scale;
            const int x1 = std::min(image.width(), x0 + scale);
            const int y1 = std::min(image.height(), y0 + scale);
            double total = 0.0;
            int count = 0;
            for (int source_y = y0; source_y < y1; ++source_y) {
                for (int source_x = x0; source_x < x1; ++source_x) {
                    total += luminance(image.pixel(source_x, source_y));
                    ++count;
                }
            }
            gray.values[static_cast<std::size_t>(y) * gray.width + x] =
                static_cast<float>(total / std::max(1, count));
        }
    }

    FeatureImage gradient;
    gradient.width = gray.width;
    gradient.height = gray.height;
    gradient.values.resize(gray.values.size());
    for (int y = 0; y < gray.height; ++y) {
        for (int x = 0; x < gray.width; ++x) {
            gradient.values[static_cast<std::size_t>(y) * gray.width + x] =
                std::abs(gray.at(x + 1, y) - gray.at(x - 1, y)) +
                std::abs(gray.at(x, y + 1) - gray.at(x, y - 1));
        }
    }
    return gradient;
}

FeatureNeighbors feature_neighbors(const NeighborImages& neighbors, int scale) {
    FeatureNeighbors result;
    if (neighbors.top) result.top = feature_image(*neighbors.top, scale);
    if (neighbors.bottom) result.bottom = feature_image(*neighbors.bottom, scale);
    if (neighbors.left) result.left = feature_image(*neighbors.left, scale);
    if (neighbors.right) result.right = feature_image(*neighbors.right, scale);
    return result;
}

std::vector<FeatureComparison> comparisons(
    const FeatureImage& source,
    const FeatureNeighbors& neighbors,
    const ImageGeometry& geometry,
    int scale) {
    const int overlap_x = std::max(1, (geometry.overlap_x + scale - 1) / scale);
    const int overlap_y = std::max(1, (geometry.overlap_y + scale - 1) / scale);
    std::vector<FeatureComparison> result;
    if (neighbors.left) {
        result.push_back({&*neighbors.left,
                          {0, 0, overlap_x, source.height},
                          {source.width - overlap_x, 0, overlap_x, source.height}});
    }
    if (neighbors.right) {
        result.push_back({&*neighbors.right,
                          {source.width - overlap_x, 0, overlap_x, source.height},
                          {0, 0, overlap_x, source.height}});
    }
    if (neighbors.top) {
        result.push_back({&*neighbors.top,
                          {0, 0, source.width, overlap_y},
                          {0, source.height - overlap_y, source.width, overlap_y}});
    }
    if (neighbors.bottom) {
        result.push_back({&*neighbors.bottom,
                          {0, source.height - overlap_y, source.width, overlap_y},
                          {0, 0, source.width, overlap_y}});
    }
    return result;
}

double image_error(const FeatureImage& source,
                   const std::vector<FeatureComparison>& regions,
                   int offset_x,
                   int offset_y,
                   int sample_step = 1) {
    if (regions.empty()) return std::numeric_limits<double>::infinity();
    double neighbor_total = 0.0;
    for (const auto& region : regions) {
        double total = 0.0;
        std::uint64_t samples = 0;
        for (int y = 0; y < region.candidate_rect.height; y += sample_step) {
            for (int x = 0; x < region.candidate_rect.width; x += sample_step) {
                const int output_x = region.candidate_rect.x + x;
                const int output_y = region.candidate_rect.y + y;
                total += std::abs(
                    source.at(output_x - offset_x, output_y - offset_y) -
                    region.neighbor->at(
                        region.neighbor_rect.x + x, region.neighbor_rect.y + y));
                ++samples;
            }
        }
        neighbor_total += total / static_cast<double>(std::max<std::uint64_t>(1, samples));
    }
    return neighbor_total / static_cast<double>(regions.size()) +
           kShiftPenalty * (std::abs(offset_x) + std::abs(offset_y));
}

bool better(const OffsetScore& candidate, const OffsetScore& current) {
    if (candidate.score != current.score) return candidate.score < current.score;
    const int candidate_distance = std::abs(candidate.x) + std::abs(candidate.y);
    const int current_distance = std::abs(current.x) + std::abs(current.y);
    if (candidate_distance != current_distance) return candidate_distance < current_distance;
    if (candidate.y != current.y) return candidate.y < current.y;
    return candidate.x < current.x;
}

OffsetScore low_resolution_candidate(
    const FeatureImage& source,
    const std::vector<FeatureComparison>& regions,
    ImageRegistrationLimits limits) {
    const int maximum_x = (limits.maximum_x + kCoarseScale - 1) / kCoarseScale;
    const int maximum_y = (limits.maximum_y + kCoarseScale - 1) / kCoarseScale;
    OffsetScore best{0, 0, image_error(source, regions, 0, 0)};
    for (int y = -maximum_y; y <= maximum_y; ++y) {
        for (int x = -maximum_x; x <= maximum_x; ++x) {
            OffsetScore candidate{x, y, image_error(source, regions, x, y)};
            if (better(candidate, best)) best = candidate;
        }
    }
    best.x = std::clamp(best.x * kCoarseScale, -limits.maximum_x, limits.maximum_x);
    best.y = std::clamp(best.y * kCoarseScale, -limits.maximum_y, limits.maximum_y);
    return best;
}

struct ProjectionProfiles {
    Rect candidate_rect;
    std::vector<double> source_columns;
    std::vector<double> neighbor_columns;
    std::vector<double> source_rows;
    std::vector<double> neighbor_rows;
};

std::vector<ProjectionProfiles> projection_profiles(
    const FeatureImage& source,
    const std::vector<FeatureComparison>& regions) {
    std::vector<ProjectionProfiles> result;
    result.reserve(regions.size());
    for (const auto& region : regions) {
        ProjectionProfiles profiles;
        profiles.candidate_rect = region.candidate_rect;
        profiles.source_columns.resize(source.width);
        profiles.neighbor_columns.resize(region.candidate_rect.width);
        profiles.source_rows.resize(source.height);
        profiles.neighbor_rows.resize(region.candidate_rect.height);
        for (int x = 0; x < source.width; ++x) {
            double total = 0.0;
            for (int y = 0; y < region.candidate_rect.height; ++y) {
                total += source.at(x, region.candidate_rect.y + y);
            }
            profiles.source_columns[x] =
                total / static_cast<double>(region.candidate_rect.height);
        }
        for (int x = 0; x < region.candidate_rect.width; ++x) {
            double total = 0.0;
            for (int y = 0; y < region.candidate_rect.height; ++y) {
                total += region.neighbor->at(
                    region.neighbor_rect.x + x, region.neighbor_rect.y + y);
            }
            profiles.neighbor_columns[x] =
                total / static_cast<double>(region.candidate_rect.height);
        }
        for (int y = 0; y < source.height; ++y) {
            double total = 0.0;
            for (int x = 0; x < region.candidate_rect.width; ++x) {
                total += source.at(region.candidate_rect.x + x, y);
            }
            profiles.source_rows[y] =
                total / static_cast<double>(region.candidate_rect.width);
        }
        for (int y = 0; y < region.candidate_rect.height; ++y) {
            double total = 0.0;
            for (int x = 0; x < region.candidate_rect.width; ++x) {
                total += region.neighbor->at(
                    region.neighbor_rect.x + x, region.neighbor_rect.y + y);
            }
            profiles.neighbor_rows[y] =
                total / static_cast<double>(region.candidate_rect.width);
        }
        result.push_back(std::move(profiles));
    }
    return result;
}

double projection_error(
    const std::vector<ProjectionProfiles>& profiles,
    int axis,
    int offset) {
    double region_total = 0.0;
    for (const auto& region : profiles) {
        double total = 0.0;
        if (axis == 0) {
            for (int x = 0; x < region.candidate_rect.width; ++x) {
                const int source_x = std::clamp(
                    region.candidate_rect.x + x - offset,
                    0, static_cast<int>(region.source_columns.size()) - 1);
                total += std::abs(
                    region.source_columns[source_x] - region.neighbor_columns[x]);
            }
            total /= static_cast<double>(region.candidate_rect.width);
        } else {
            for (int y = 0; y < region.candidate_rect.height; ++y) {
                const int source_y = std::clamp(
                    region.candidate_rect.y + y - offset,
                    0, static_cast<int>(region.source_rows.size()) - 1);
                total += std::abs(
                    region.source_rows[source_y] - region.neighbor_rows[y]);
            }
            total /= static_cast<double>(region.candidate_rect.height);
        }
        region_total += total;
    }
    return region_total / static_cast<double>(profiles.size());
}

OffsetScore projection_candidate(
    const FeatureImage& source,
    const std::vector<FeatureComparison>& regions,
    ImageRegistrationLimits limits) {
    const auto profiles = projection_profiles(source, regions);
    int best_x = 0;
    int best_y = 0;
    double best_x_score = projection_error(profiles, 0, 0);
    double best_y_score = projection_error(profiles, 1, 0);
    for (int x = -limits.maximum_x; x <= limits.maximum_x; ++x) {
        const double score = projection_error(profiles, 0, x);
        if (score < best_x_score ||
            (score == best_x_score && std::abs(x) < std::abs(best_x))) {
            best_x = x;
            best_x_score = score;
        }
    }
    for (int y = -limits.maximum_y; y <= limits.maximum_y; ++y) {
        const double score = projection_error(profiles, 1, y);
        if (score < best_y_score ||
            (score == best_y_score && std::abs(y) < std::abs(best_y))) {
            best_y = y;
            best_y_score = score;
        }
    }
    return {best_x, best_y, best_x_score + best_y_score};
}

OffsetScore refine_candidate(
    OffsetScore coarse,
    const FeatureImage& source,
    const std::vector<FeatureComparison>& regions,
    ImageRegistrationLimits limits) {
    OffsetScore best{
        coarse.x, coarse.y,
        image_error(source, regions, coarse.x, coarse.y, kValidationSampleStep)};
    for (int y = std::max(-limits.maximum_y, coarse.y - kRefineRadius);
         y <= std::min(limits.maximum_y, coarse.y + kRefineRadius); ++y) {
        for (int x = std::max(-limits.maximum_x, coarse.x - kRefineRadius);
             x <= std::min(limits.maximum_x, coarse.x + kRefineRadius); ++x) {
            OffsetScore candidate{
                x, y, image_error(source, regions, x, y, kValidationSampleStep)};
            if (better(candidate, best)) best = candidate;
        }
    }
    return best;
}

double improvement(double before, double after) {
    return before > 0.0 ? (before - after) / before : 0.0;
}

ImageRegistrationCandidate registration_candidate(
    ImageRegistrationMethod method,
    OffsetScore candidate,
    double score_before,
    ImageRegistrationLimits limits) {
    ImageRegistrationCandidate result;
    result.method = method;
    result.offset_x = candidate.x;
    result.offset_y = candidate.y;
    result.score = candidate.score;
    result.relative_improvement = improvement(score_before, candidate.score);
    const bool at_boundary =
        (limits.maximum_x > 0 && std::abs(candidate.x) == limits.maximum_x) ||
        (limits.maximum_y > 0 && std::abs(candidate.y) == limits.maximum_y);
    result.accepted = (candidate.x != 0 || candidate.y != 0) && !at_boundary &&
                      result.relative_improvement >= kMinimumRelativeImprovement;
    result.evaluated = true;
    return result;
}

const ImageRegistrationCandidate* choose_candidate(
    const ImageRegistrationComparison& comparison) {
    const auto& low = comparison.low_resolution;
    const auto& projection = comparison.projection;
    if (low.accepted && projection.accepted) {
        return low.score <= projection.score ? &low : &projection;
    }
    if (low.accepted) return &low;
    if (projection.accepted) return &projection;
    return nullptr;
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

}  // namespace

const char* registration_method_name(ImageRegistrationMethod method) {
    switch (method) {
    case ImageRegistrationMethod::LowResolution2D: return "low_resolution_2d";
    case ImageRegistrationMethod::Projection: return "projection";
    }
    return "unknown";
}

Result<ImageRegistrationLimits> ImageRegistration::limits(const ProjectConfig& config) {
    auto geometry = image_geometry(config);
    if (!geometry) {
        return Result<ImageRegistrationLimits>::failure(
            geometry.error().code, geometry.error().message);
    }
    return Result<ImageRegistrationLimits>::success({
        std::min(kMaximumHorizontalShift, geometry.value().overlap_x * 2 / 3),
        std::min(kMaximumVerticalShift, geometry.value().overlap_y * 2 / 3)});
}

Result<ImageBuffer> ImageRegistration::translate(
    const ImageBuffer& source,
    const ProjectConfig& config,
    int offset_x,
    int offset_y) {
    if (!config.has_chunk_size() || source.width() != *config.chunk_width ||
        source.height() != *config.chunk_height) {
        return Result<ImageBuffer>::failure(
            "registration_size_mismatch",
            "Image registration requires a normalized chunk image.");
    }
    auto allowed = limits(config);
    if (!allowed) return Result<ImageBuffer>::failure(
        allowed.error().code, allowed.error().message);
    if (std::abs(offset_x) > allowed.value().maximum_x ||
        std::abs(offset_y) > allowed.value().maximum_y) {
        return Result<ImageBuffer>::failure(
            "registration_offset_out_of_range",
            "Chunk translation exceeds the safe registration range.");
    }
    return Result<ImageBuffer>::success(shifted_image(source, offset_x, offset_y));
}

Result<ImageRegistrationResult> ImageRegistration::align(
    const ImageBuffer& source,
    const ProjectConfig& config,
    const NeighborImages& neighbors) {
    auto allowed = limits(config);
    if (!allowed) return Result<ImageRegistrationResult>::failure(
        allowed.error().code, allowed.error().message);
    if (!config.has_chunk_size() || source.width() != *config.chunk_width ||
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
    auto geometry = image_geometry(config);
    if (!geometry) return Result<ImageRegistrationResult>::failure(
        geometry.error().code, geometry.error().message);

    const auto coarse_source = feature_image(source, kCoarseScale);
    const auto coarse_neighbors = feature_neighbors(neighbors, kCoarseScale);
    const auto coarse_regions = comparisons(
        coarse_source, coarse_neighbors, geometry.value(), kCoarseScale);
    const auto full_source = feature_image(source, 1);
    const auto full_neighbors = feature_neighbors(neighbors, 1);
    const auto full_regions = comparisons(full_source, full_neighbors, geometry.value(), 1);
    const double score_before = image_error(
        full_source, full_regions, 0, 0, kValidationSampleStep);

    const auto low_coarse = low_resolution_candidate(
        coarse_source, coarse_regions, allowed.value());
    const auto projection_coarse = projection_candidate(
        full_source, full_regions, allowed.value());
    const auto low_refined = refine_candidate(
        low_coarse, full_source, full_regions, allowed.value());
    const auto projection_refined = refine_candidate(
        projection_coarse, full_source, full_regions, allowed.value());

    result.comparison.low_resolution = registration_candidate(
        ImageRegistrationMethod::LowResolution2D,
        low_refined, score_before, allowed.value());
    result.comparison.projection = registration_candidate(
        ImageRegistrationMethod::Projection,
        projection_refined, score_before, allowed.value());
    const auto* selected = choose_candidate(result.comparison);
    result.score_before = score_before;
    result.score_after = score_before;
    if (selected) {
        result.comparison.selected_method = selected->method;
        result.offset_x = selected->offset_x;
        result.offset_y = selected->offset_y;
        result.score_after = selected->score;
        result.relative_improvement = selected->relative_improvement;
        result.applied = true;
        result.image = shifted_image(source, result.offset_x, result.offset_y);
    } else {
        result.comparison.selected_method =
            result.comparison.low_resolution.score <= result.comparison.projection.score
                ? ImageRegistrationMethod::LowResolution2D
                : ImageRegistrationMethod::Projection;
    }
    return Result<ImageRegistrationResult>::success(std::move(result));
}

}  // namespace chunkmap
