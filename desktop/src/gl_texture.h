#pragma once

#include "core/result.h"
#include "image/image_buffer.h"

#include <filesystem>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace chunkmap_desktop {

class GlTexture {
public:
    GlTexture() = default;
    ~GlTexture();
    GlTexture(const GlTexture&) = delete;
    GlTexture& operator=(const GlTexture&) = delete;

    chunkmap::Result<void> load(const std::filesystem::path& path);
    chunkmap::Result<void> load(const chunkmap::ImageBuffer& image);
    void reset();

    unsigned int id() const { return id_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    unsigned int id_ = 0;
    int width_ = 0;
    int height_ = 0;
};

class TextureCache {
public:
    struct LoadEvent {
        std::filesystem::path path;
        bool success = false;
        double decode_ms = 0.0;
        double upload_ms = 0.0;
        std::string error;
    };

    TextureCache();
    ~TextureCache();
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    GlTexture* get(const std::filesystem::path& path);
    chunkmap::Result<void> put(const std::filesystem::path& path,
                               const chunkmap::ImageBuffer& image);
    void invalidate(const std::filesystem::path& path);
    void clear();
    std::vector<LoadEvent> take_load_events();
    std::size_t decoder_worker_count() const { return loader_threads_.size(); }
    const std::string& last_error() const { return last_error_; }

private:
    struct Entry {
        std::unique_ptr<GlTexture> texture;
        std::filesystem::file_time_type modified{};
    };

    struct LoadIdentity {
        std::filesystem::file_time_type modified{};
        std::uint64_t generation = 0;

        bool operator==(const LoadIdentity& other) const {
            return generation == other.generation && modified == other.modified;
        }
    };

    struct LoadTask {
        std::filesystem::path path;
        LoadIdentity identity;
    };

    struct DecodeResult {
        std::filesystem::path path;
        LoadIdentity identity;
        std::optional<chunkmap::ImageBuffer> image;
        double decode_ms = 0.0;
        std::string error;
    };

    void loader_loop();
    void forget_pending(const std::filesystem::path& path);

    std::map<std::filesystem::path, Entry> entries_;
    std::vector<LoadEvent> load_events_;
    std::string last_error_;
    std::mutex loader_mutex_;
    std::condition_variable loader_condition_;
    std::deque<LoadTask> pending_loads_;
    std::map<std::filesystem::path, LoadIdentity> requested_loads_;
    std::map<std::filesystem::path, DecodeResult> decoded_loads_;
    std::uint64_t generation_ = 0;
    bool loader_stopping_ = false;
    std::vector<std::thread> loader_threads_;
};

}  // namespace chunkmap_desktop
