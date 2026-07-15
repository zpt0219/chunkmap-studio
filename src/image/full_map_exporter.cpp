#include "image/full_map_exporter.h"

#include "image/png_stream_writer.h"
#include "image/layout_renderer.h"
#include "io/atomic_file.h"
#include "ui/map_geometry.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <limits>
#include <system_error>
#include <vector>

namespace chunkmap {

namespace {

bool path_is_within(const std::filesystem::path& candidate,
                    const std::filesystem::path& root) {
    auto candidate_part = candidate.begin();
    for (auto root_part = root.begin(); root_part != root.end(); ++root_part, ++candidate_part) {
        if (candidate_part == candidate.end() || *candidate_part != *root_part) return false;
    }
    return true;
}

Result<std::filesystem::path> validate_output_path(
    const Project& project, const FullMapExportOptions& options) {
    if (!options.output.is_absolute()) {
        return Result<std::filesystem::path>::failure(
            "invalid_export_path", "Full map export requires an absolute output path.");
    }
    std::string extension = options.output.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension != ".png") {
        return Result<std::filesystem::path>::failure(
            "invalid_export_path", "Full map export output must use the .png extension.");
    }
    std::error_code error;
    if (!std::filesystem::is_directory(options.output.parent_path(), error) || error) {
        return Result<std::filesystem::path>::failure(
            "export_parent_missing", "Export parent directory does not exist.");
    }
    if (std::filesystem::is_symlink(options.output, error) && !error) {
        return Result<std::filesystem::path>::failure(
            "invalid_export_path", "Export output cannot be a symbolic link.");
    }
    error.clear();
    if (std::filesystem::is_directory(options.output, error) && !error) {
        return Result<std::filesystem::path>::failure(
            "invalid_export_path", "Export output must be a file path.");
    }
    error.clear();
    const bool exists = std::filesystem::exists(options.output, error) && !error;
    if (exists && !options.overwrite) {
        return Result<std::filesystem::path>::failure(
            "export_exists", "Export output already exists; use --force to replace it.");
    }
    error.clear();
    const auto canonical_parent = std::filesystem::weakly_canonical(
        options.output.parent_path(), error);
    if (error) {
        return Result<std::filesystem::path>::failure(
            "invalid_export_path", "Unable to normalize export output path.");
    }
    const auto normalized_output = (canonical_parent / options.output.filename()).lexically_normal();
    const auto project_root = std::filesystem::weakly_canonical(project.paths.root(), error);
    if (error) {
        return Result<std::filesystem::path>::failure(
            "invalid_export_path", "Unable to normalize project path.");
    }
    if (path_is_within(normalized_output, project_root)) {
        return Result<std::filesystem::path>::failure(
            "export_inside_project", "Full map export must be written outside the project directory.");
    }
    return Result<std::filesystem::path>::success(normalized_output);
}

void remove_if_present(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
}

struct TempFileCleanup {
    std::filesystem::path path;
    bool keep = false;

    ~TempFileCleanup() {
        if (!keep) remove_if_present(path);
    }
};

void blit_to_band(const ImageBuffer& image,
                  int world_x,
                  int world_y,
                  int band_y,
                  int band_rows,
                  std::size_t row_bytes,
                  std::vector<std::uint8_t>& band) {
    const int source_top = std::max(0, band_y - world_y);
    const int source_bottom = std::min(image.height(), band_y + band_rows - world_y);
    const int source_left = std::max(0, -world_x);
    const int source_right = std::min(
        image.width(), static_cast<int>(row_bytes / 4U) - world_x);
    if (source_top >= source_bottom || source_left >= source_right) return;
    for (int source_y = source_top; source_y < source_bottom; ++source_y) {
        const int destination_y = world_y + source_y - band_y;
        const auto* source = image.pixel(source_left, source_y);
        auto* destination = band.data() +
            static_cast<std::size_t>(destination_y) * row_bytes +
            static_cast<std::size_t>(world_x + source_left) * 4U;
        std::copy_n(source, static_cast<std::size_t>(source_right - source_left) * 4U,
                    destination);
    }
}

}  // namespace

Result<FullMapExportResult> export_full_map(
    ProjectDocument& document, const FullMapExportOptions& options,
    const FullMapExportProgress& progress) {
    if (!document.config().has_chunk_size()) {
        return Result<FullMapExportResult>::failure(
            "missing_chunk_dimensions", "Import a chunk image before exporting the full map.");
    }
    if (document.ready_count() == 0) {
        return Result<FullMapExportResult>::failure(
            "no_ready_chunks", "Full map export requires at least one Ready chunk.");
    }
    auto output = validate_output_path(document.project(), options);
    if (!output) return Result<FullMapExportResult>::failure(
        output.error().code, output.error().message);
    auto geometry = map_geometry(document.config());
    if (!geometry) return Result<FullMapExportResult>::failure(
        geometry.error().code, geometry.error().message);

    const std::size_t world_width = static_cast<std::size_t>(geometry.value().world_width);
    if (world_width > std::numeric_limits<std::size_t>::max() / 4U) {
        return Result<FullMapExportResult>::failure(
            "export_row_too_large", "Full map export row is too large.");
    }
    const std::size_t row_bytes = world_width * 4U;
    if (row_bytes > std::numeric_limits<unsigned int>::max()) {
        return Result<FullMapExportResult>::failure(
            "export_row_too_large", "Full map export row exceeds PNG encoder limits.");
    }
    const std::size_t band_limit = std::max(row_bytes, options.maximum_band_bytes);
    const std::size_t band_height_value = std::max<std::size_t>(
        1U, std::min<std::size_t>(static_cast<std::size_t>(geometry.value().chunk_height),
                                 band_limit / row_bytes));
    if (band_height_value > std::numeric_limits<std::size_t>::max() / row_bytes) {
        return Result<FullMapExportResult>::failure(
            "image_too_large", "Full map export band is too large.");
    }

    std::size_t total_work = 1U;
    for (int band_y = 0; band_y < geometry.value().world_height;
         band_y += static_cast<int>(band_height_value)) {
        const int rows = std::min(
            static_cast<int>(band_height_value), geometry.value().world_height - band_y);
        ++total_work;
        for (int y = 0; y < document.config().rows; ++y) {
            const int chunk_top = y * geometry.value().step_y;
            const int source_top = std::max(0, band_y - chunk_top);
            const int source_bottom = std::min(
                geometry.value().chunk_height, band_y + rows - chunk_top);
            if (source_top >= source_bottom) continue;
            for (int x = 0; x < document.config().columns; ++x) {
                if (document.chunk({x, y}).ready) ++total_work;
            }
        }
    }
    std::size_t completed_work = 0U;
    if (progress) progress(completed_work, total_work, "Preparing PNG output");

    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path temp =
        output.value().string() + ".tmp-" + std::to_string(ticks);
    TempFileCleanup cleanup{temp};
    PngStreamWriter writer;
    auto opened = writer.open(temp, geometry.value().world_width, geometry.value().world_height);
    if (!opened) {
        return Result<FullMapExportResult>::failure(opened.error().code, opened.error().message);
    }

    std::vector<std::uint8_t> band(row_bytes * band_height_value, 0U);
    for (int band_y = 0; band_y < geometry.value().world_height;
         band_y += static_cast<int>(band_height_value)) {
        const int rows = std::min(
            static_cast<int>(band_height_value), geometry.value().world_height - band_y);
        std::fill(band.begin(), band.begin() + static_cast<std::size_t>(rows) * row_bytes, 0U);

        for (int y = 0; y < document.config().rows; ++y) {
            const int chunk_top = y * geometry.value().step_y;
            const int source_top = std::max(0, band_y - chunk_top);
            const int source_bottom = std::min(
                geometry.value().chunk_height, band_y + rows - chunk_top);
            if (source_top >= source_bottom) continue;
            for (int x = 0; x < document.config().columns; ++x) {
                const ChunkCoord coord{x, y};
                if (!document.chunk(coord).ready) continue;
                if (progress) {
                    progress(completed_work, total_work,
                             "Compositing chunk " + coord_name(coord));
                }
                auto image = document.image(coord);
                if (!image) {
                    return Result<FullMapExportResult>::failure(
                        image.error().code, image.error().message);
                }
                auto placed = LayoutRenderer::render_placed_chunk(
                    *image.value(), document.config(), document.placement(coord));
                if (!placed) return Result<FullMapExportResult>::failure(
                    placed.error().code, placed.error().message);
                blit_to_band(placed.value(), x * geometry.value().step_x, chunk_top,
                             band_y, rows, row_bytes, band);
                ++completed_work;
                if (progress) {
                    progress(completed_work, total_work,
                             "Composited chunk " + coord_name(coord));
                }
            }
        }

        // Seam patches are the only derived pixels. Draw vertical seams first,
        // then horizontal seams, matching the Desktop canvas order.
        for (const auto direction : {SeamDirection::Right, SeamDirection::Bottom}) {
            for (int y = 0; y < document.config().rows; ++y) {
                for (int x = 0; x < document.config().columns; ++x) {
                    const SeamKey key{{x, y}, direction};
                    const ChunkCoord second = seam_second(key);
                    if (!document.config().contains(second) ||
                        !document.chunk(key.first).ready || !document.chunk(second).ready) {
                        continue;
                    }
                    auto first_image = document.image(key.first);
                    auto second_image = document.image(second);
                    if (!first_image || !second_image) {
                        const auto& error = !first_image ? first_image.error() : second_image.error();
                        return Result<FullMapExportResult>::failure(error.code, error.message);
                    }
                    const SeamDefinition seam = document.seam_override(key)
                        ? *document.seam_override(key)
                        : LayoutRenderer::default_seam(document.config(), key);
                    auto patch = LayoutRenderer::render_seam_patch(
                        *first_image.value(), *second_image.value(), document.config(),
                        document.placement(key.first), document.placement(second), seam);
                    if (!patch) return Result<FullMapExportResult>::failure(
                        patch.error().code, patch.error().message);
                    const int patch_x = direction == SeamDirection::Right
                        ? second.x * geometry.value().step_x
                        : key.first.x * geometry.value().step_x;
                    const int patch_y = direction == SeamDirection::Bottom
                        ? second.y * geometry.value().step_y
                        : key.first.y * geometry.value().step_y;
                    blit_to_band(patch.value(), patch_x, patch_y,
                                 band_y, rows, row_bytes, band);
                }
            }
        }
        if (progress) {
            progress(completed_work, total_work,
                     "Encoding rows " + std::to_string(band_y + 1) + "-" +
                         std::to_string(band_y + rows));
        }
        auto written = writer.write_rows(band.data(), rows, row_bytes);
        if (!written) {
            return Result<FullMapExportResult>::failure(
                written.error().code, written.error().message);
        }
        ++completed_work;
        if (progress) {
            progress(completed_work, total_work,
                     "Encoded rows " + std::to_string(band_y + 1) + "-" +
                         std::to_string(band_y + rows));
        }
    }
    if (progress) progress(completed_work, total_work, "Finalizing PNG file");
    auto finished = writer.finish();
    if (!finished) {
        return Result<FullMapExportResult>::failure(
            finished.error().code, finished.error().message);
    }
    std::error_code error;
    if (!options.overwrite && std::filesystem::exists(output.value(), error) && !error) {
        return Result<FullMapExportResult>::failure(
            "export_exists", "Export output already exists; use --force to replace it.");
    }
    auto replaced = atomic_file::replace(temp, output.value());
    if (!replaced) return Result<FullMapExportResult>::failure(
        "export_write_failed", replaced.error().message);
    cleanup.keep = true;
    ++completed_work;
    if (progress) progress(completed_work, total_work, "Export complete");

    FullMapExportResult result;
    result.output = output.value();
    result.width = geometry.value().world_width;
    result.height = geometry.value().world_height;
    result.ready_chunks = document.ready_count();
    result.empty_chunks = document.config().columns * document.config().rows - result.ready_chunks;
    return Result<FullMapExportResult>::success(std::move(result));
}

}  // namespace chunkmap
