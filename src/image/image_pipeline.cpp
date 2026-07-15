#include "image/image_pipeline.h"

#include <algorithm>
#include <cmath>

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

Result<ImageBuffer> ConceptSlicer::slice_one(const ImageBuffer& concept,
                                             int columns,
                                             int rows,
                                             ChunkCoord coord) {
    if (concept.empty() || columns <= 0 || rows <= 0 ||
        columns > concept.width() || rows > concept.height()) {
        return Result<ImageBuffer>::failure(
            "invalid_concept_grid", "Concept grid must fit within the concept image.");
    }
    if (coord.x < 0 || coord.y < 0 || coord.x >= columns || coord.y >= rows) {
        return Result<ImageBuffer>::failure(
            "chunk_out_of_range", "Chunk coordinate is outside the concept grid.");
    }
    const int x0 = coord.x * concept.width() / columns;
    const int x1 = (coord.x + 1) * concept.width() / columns;
    const int y0 = coord.y * concept.height() / rows;
    const int y1 = (coord.y + 1) * concept.height() / rows;
    return concept.crop({x0, y0, x1 - x0, y1 - y0});
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
        for (int x = 0; x < columns; ++x) {
            auto region = slice_one(concept, columns, rows, {x, y});
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

}  // namespace chunkmap
