#include "model/project_document.h"

#include "io/atomic_file.h"

#include <filesystem>

namespace chunkmap {

namespace {

std::string read_optional_text(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) return {};
    auto content = atomic_file::read_text(path);
    return content ? content.take_value() : std::string{};
}

bool regular_file_exists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

}  // namespace

Result<ProjectDocument> ProjectDocument::load(Project project) {
    ProjectDocument document;
    document.project_ = std::move(project);
    document.global_prompt_ = read_optional_text(document.project_.paths.global_prompt());
    document.chunks_.resize(static_cast<std::size_t>(document.config().columns) *
                            static_cast<std::size_t>(document.config().rows));
    for (int y = 0; y < document.config().rows; ++y) {
        for (int x = 0; x < document.config().columns; ++x) {
            const ChunkCoord coord{x, y};
            auto& chunk = document.chunk(coord);
            chunk.prompt = read_optional_text(document.project_.paths.chunk_prompt(coord));
            chunk.ready = regular_file_exists(document.project_.paths.chunk_image(coord));
            if (chunk.ready) {
                std::error_code error;
                chunk.image_modified = std::filesystem::last_write_time(
                    document.project_.paths.chunk_image(coord), error);
                if (error) chunk.image_modified = {};
            }
        }
    }
    return Result<ProjectDocument>::success(std::move(document));
}

std::size_t ProjectDocument::index(ChunkCoord coord) const {
    return static_cast<std::size_t>(coord.y) * static_cast<std::size_t>(config().columns) +
           static_cast<std::size_t>(coord.x);
}

ChunkDocument& ProjectDocument::chunk(ChunkCoord coord) { return chunks_.at(index(coord)); }
const ChunkDocument& ProjectDocument::chunk(ChunkCoord coord) const { return chunks_.at(index(coord)); }

Result<const ImageBuffer*> ProjectDocument::image(ChunkCoord coord) const {
    auto& state = chunks_.at(index(coord));
    if (!state.ready) {
        return Result<const ImageBuffer*>::failure("chunk_empty", "Chunk is Empty.");
    }
    if (!state.image_loaded) {
        std::error_code error;
        const auto modified = std::filesystem::last_write_time(
            project_.paths.chunk_image(coord), error);
        if (error || modified != state.image_modified) {
            return Result<const ImageBuffer*>::failure(
                "external_project_change",
                "A Ready chunk changed outside the current session. Reload the project.");
        }
        auto loaded = ImageBuffer::load(project_.paths.chunk_image(coord));
        if (!loaded) {
            return Result<const ImageBuffer*>::failure(loaded.error().code, loaded.error().message);
        }
        state.image = loaded.take_value();
        state.image_loaded = true;
    }
    touch_and_trim(coord);
    return Result<const ImageBuffer*>::success(&*state.image);
}

void ProjectDocument::replace_image(ChunkCoord coord, ImageBuffer image) {
    auto& state = chunk(coord);
    state.ready = true;
    state.image = std::move(image);
    state.image_loaded = true;
    std::error_code error;
    state.image_modified = std::filesystem::last_write_time(
        project_.paths.chunk_image(coord), error);
    if (error) state.image_modified = {};
    touch_and_trim(coord);
}

void ProjectDocument::remove_image(ChunkCoord coord) {
    auto& state = chunk(coord);
    state.ready = false;
    state.image.reset();
    state.image_loaded = false;
    state.image_modified = {};
}

void ProjectDocument::reset_empty_grid() {
    chunks_.assign(
        static_cast<std::size_t>(config().columns) * static_cast<std::size_t>(config().rows),
        ChunkDocument{});
    access_clock_ = 0;
}

int ProjectDocument::ready_count() const {
    int count = 0;
    for (const auto& state : chunks_) count += state.ready ? 1 : 0;
    return count;
}

int ProjectDocument::prompt_count() const {
    int count = 0;
    for (const auto& state : chunks_) count += state.prompt.empty() ? 0 : 1;
    return count;
}

int ProjectDocument::cached_image_count() const {
    int count = 0;
    for (const auto& state : chunks_) count += state.image_loaded ? 1 : 0;
    return count;
}

void ProjectDocument::touch_and_trim(ChunkCoord preserved) const {
    constexpr int kMaximumCachedImages = 16;
    const auto preserved_index = index(preserved);
    chunks_[preserved_index].last_image_access = ++access_clock_;
    while (cached_image_count() > kMaximumCachedImages) {
        std::size_t oldest_index = chunks_.size();
        std::size_t oldest_access = access_clock_ + 1;
        for (std::size_t current = 0; current < chunks_.size(); ++current) {
            const auto& state = chunks_[current];
            if (current != preserved_index && state.image_loaded &&
                state.last_image_access < oldest_access) {
                oldest_index = current;
                oldest_access = state.last_image_access;
            }
        }
        if (oldest_index == chunks_.size()) break;
        chunks_[oldest_index].image.reset();
        chunks_[oldest_index].image_loaded = false;
    }
}

}  // namespace chunkmap
