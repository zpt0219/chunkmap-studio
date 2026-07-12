#include "image/image_pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace chunkmap {

namespace {

Result<void> validate_neighbor(const std::optional<ImageBuffer>& image,
                               int width,
                               int height) {
    if (image && (image->width() != width || image->height() != height)) {
        return Result<void>::failure(
            "neighbor_size_mismatch", "A neighbor image does not match the project chunk size.");
    }
    return Result<void>::success();
}

ImageBuffer pad_image(const ImageBuffer& source,
                      int target_width,
                      int target_height,
                      int added_left,
                      int added_top) {
    ImageBuffer result(target_width, target_height);
    for (int y = 0; y < target_height; ++y) {
        const int source_y = std::clamp(y - added_top, 0, source.height() - 1);
        for (int x = 0; x < target_width; ++x) {
            const int source_x = std::clamp(x - added_left, 0, source.width() - 1);
            std::copy_n(source.pixel(source_x, source_y), 4, result.pixel(x, y));
        }
    }
    return result;
}

double edge_error(const ImageBuffer& candidate,
                  const ProjectConfig& config,
                  const NeighborImages& neighbors,
                  const ImageGeometry& geometry) {
    double total = 0.0;
    std::uint64_t samples = 0;
    auto compare = [&](const ImageBuffer& neighbor, Rect candidate_rect, Rect neighbor_rect) {
        for (int y = 0; y < candidate_rect.height; ++y) {
            for (int x = 0; x < candidate_rect.width; ++x) {
                const auto* a = candidate.pixel(candidate_rect.x + x, candidate_rect.y + y);
                const auto* b = neighbor.pixel(neighbor_rect.x + x, neighbor_rect.y + y);
                for (int channel = 0; channel < 3; ++channel) {
                    total += std::abs(static_cast<int>(a[channel]) - static_cast<int>(b[channel]));
                    ++samples;
                }
            }
        }
    };

    const int width = *config.chunk_width;
    const int height = *config.chunk_height;
    if (neighbors.left) {
        compare(*neighbors.left, {0, 0, geometry.overlap_x, height},
                {width - geometry.overlap_x, 0, geometry.overlap_x, height});
    }
    if (neighbors.right) {
        compare(*neighbors.right, {width - geometry.overlap_x, 0, geometry.overlap_x, height},
                {0, 0, geometry.overlap_x, height});
    }
    if (neighbors.top) {
        compare(*neighbors.top, {0, 0, width, geometry.overlap_y},
                {0, height - geometry.overlap_y, width, geometry.overlap_y});
    }
    if (neighbors.bottom) {
        compare(*neighbors.bottom, {0, height - geometry.overlap_y, width, geometry.overlap_y},
                {0, 0, width, geometry.overlap_y});
    }
    return samples == 0 ? 0.0 : total / static_cast<double>(samples);
}

}  // namespace

int NeighborImages::count() const {
    return static_cast<int>(top.has_value()) + static_cast<int>(bottom.has_value()) +
           static_cast<int>(left.has_value()) + static_cast<int>(right.has_value());
}

Result<ImageGeometry> image_geometry(const ProjectConfig& config) {
    if (!config.has_chunk_size()) {
        return Result<ImageGeometry>::failure(
            "missing_chunk_size", "Import a chunk image before using the image pipeline.");
    }
    if (*config.chunk_width < 2 || *config.chunk_height < 2) {
        return Result<ImageGeometry>::failure(
            "chunk_too_small", "Chunk dimensions must be at least 2x2 for overlap processing.");
    }
    ImageGeometry result;
    result.overlap_x = std::clamp(
        static_cast<int>(std::lround(*config.chunk_width * config.horizontal_overlap_ratio)),
        1, *config.chunk_width - 1);
    result.overlap_y = std::clamp(
        static_cast<int>(std::lround(*config.chunk_height * config.vertical_overlap_ratio)),
        1, *config.chunk_height - 1);
    result.step_x = *config.chunk_width - result.overlap_x;
    result.step_y = *config.chunk_height - result.overlap_y;
    return Result<ImageGeometry>::success(result);
}

Result<std::vector<ImageBuffer>> ConceptSlicer::slice(const ImageBuffer& concept,
                                                      int columns,
                                                      int rows) {
    if (concept.empty() || columns <= 0 || rows <= 0 ||
        columns > concept.width() || rows > concept.height()) {
        return Result<std::vector<ImageBuffer>>::failure(
            "invalid_concept_grid", "Concept grid must fit within the concept image.");
    }
    std::vector<ImageBuffer> regions;
    regions.reserve(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows));
    for (int y = 0; y < rows; ++y) {
        const int y0 = y * concept.height() / rows;
        const int y1 = (y + 1) * concept.height() / rows;
        for (int x = 0; x < columns; ++x) {
            const int x0 = x * concept.width() / columns;
            const int x1 = (x + 1) * concept.width() / columns;
            auto region = concept.crop({x0, y0, x1 - x0, y1 - y0});
            if (!region) {
                return Result<std::vector<ImageBuffer>>::failure(
                    region.error().code, region.error().message);
            }
            regions.push_back(region.take_value());
        }
    }
    return Result<std::vector<ImageBuffer>>::success(std::move(regions));
}

Result<ImageBuffer> TemplateBuilder::build(const ProjectConfig& config,
                                           const NeighborImages& neighbors) {
    auto geometry = image_geometry(config);
    if (!geometry) return Result<ImageBuffer>::failure(geometry.error().code, geometry.error().message);
    if (neighbors.count() == 0) {
        return Result<ImageBuffer>::failure(
            "no_ready_neighbor", "Chunk context requires at least one Ready orthogonal neighbor.");
    }
    const int width = *config.chunk_width;
    const int height = *config.chunk_height;
    for (const auto* neighbor : {&neighbors.top, &neighbors.bottom, &neighbors.left, &neighbors.right}) {
        auto valid = validate_neighbor(*neighbor, width, height);
        if (!valid) return Result<ImageBuffer>::failure(valid.error().code, valid.error().message);
    }

    ImageBuffer result(width, height);
    // Fixed order is part of the file format: later sides own overlapping corners.
    if (neighbors.top) {
        auto copied = result.blit(*neighbors.top,
            {0, height - geometry.value().overlap_y, width, geometry.value().overlap_y}, {0, 0});
        if (!copied) return Result<ImageBuffer>::failure(copied.error().code, copied.error().message);
    }
    if (neighbors.bottom) {
        auto copied = result.blit(*neighbors.bottom,
            {0, 0, width, geometry.value().overlap_y}, {0, height - geometry.value().overlap_y});
        if (!copied) return Result<ImageBuffer>::failure(copied.error().code, copied.error().message);
    }
    if (neighbors.left) {
        auto copied = result.blit(*neighbors.left,
            {width - geometry.value().overlap_x, 0, geometry.value().overlap_x, height}, {0, 0});
        if (!copied) return Result<ImageBuffer>::failure(copied.error().code, copied.error().message);
    }
    if (neighbors.right) {
        auto copied = result.blit(*neighbors.right,
            {0, 0, geometry.value().overlap_x, height}, {width - geometry.value().overlap_x, 0});
        if (!copied) return Result<ImageBuffer>::failure(copied.error().code, copied.error().message);
    }
    return Result<ImageBuffer>::success(std::move(result));
}

Result<NormalizedImage> ImageNormalizer::normalize(const ImageBuffer& source,
                                                   const ProjectConfig& config,
                                                   ChunkCoord coord,
                                                   const NeighborImages& neighbors) {
    auto geometry = image_geometry(config);
    if (!geometry) return Result<NormalizedImage>::failure(geometry.error().code, geometry.error().message);
    const int target_width = *config.chunk_width;
    const int target_height = *config.chunk_height;
    for (const auto* neighbor : {
             &neighbors.top, &neighbors.bottom, &neighbors.left, &neighbors.right}) {
        auto valid = validate_neighbor(*neighbor, target_width, target_height);
        if (!valid) {
            return Result<NormalizedImage>::failure(
                valid.error().code, valid.error().message);
        }
    }
    const int missing_x = target_width - source.width();
    const int missing_y = target_height - source.height();
    if (missing_x == 0 && missing_y == 0) {
        return Result<NormalizedImage>::success({source, 0, 0, 0, 0});
    }
    if (missing_x < 0 || missing_y < 0 || missing_x > 1 || missing_y > 1) {
        return Result<NormalizedImage>::failure(
            "chunk_size_mismatch",
            "Chunk image must match the project size or be at most 1px smaller per dimension.");
    }

    struct Candidate {
        NormalizedImage normalized;
        double error = std::numeric_limits<double>::infinity();
        int preference = 0;
    };
    std::vector<Candidate> candidates;
    const int left_options = missing_x == 0 ? 1 : 2;
    const int top_options = missing_y == 0 ? 1 : 2;
    for (int added_left = 0; added_left < left_options; ++added_left) {
        for (int added_top = 0; added_top < top_options; ++added_top) {
            Candidate candidate;
            candidate.normalized.image = pad_image(
                source, target_width, target_height, added_left, added_top);
            candidate.normalized.added_left = added_left;
            candidate.normalized.added_top = added_top;
            candidate.normalized.added_right = missing_x - added_left;
            candidate.normalized.added_bottom = missing_y - added_top;
            candidate.error = edge_error(
                candidate.normalized.image, config, neighbors, geometry.value());

            const int preferred_left = missing_x == 0 ? 0 :
                (coord.x == 0 ? 1 : (coord.x == config.columns - 1 ? 0 : 0));
            const int preferred_top = missing_y == 0 ? 0 :
                (coord.y == 0 ? 1 : (coord.y == config.rows - 1 ? 0 : 0));
            candidate.preference = (added_left != preferred_left) +
                                   (added_top != preferred_top);
            candidates.push_back(std::move(candidate));
        }
    }
    const auto best = std::min_element(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) {
            if (a.error != b.error) return a.error < b.error;
            return a.preference < b.preference;
        });
    return Result<NormalizedImage>::success(best->normalized);
}

}  // namespace chunkmap
