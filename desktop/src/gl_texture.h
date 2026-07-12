#pragma once

#include "core/result.h"
#include "image/image_buffer.h"

#include <filesystem>
#include <map>
#include <memory>

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
    GlTexture* get(const std::filesystem::path& path);
    chunkmap::Result<void> put(const std::filesystem::path& path,
                               const chunkmap::ImageBuffer& image);
    void invalidate(const std::filesystem::path& path);
    void clear();
    const std::string& last_error() const { return last_error_; }

private:
    struct Entry {
        std::unique_ptr<GlTexture> texture;
        std::filesystem::file_time_type modified{};
    };

    std::map<std::filesystem::path, Entry> entries_;
    std::string last_error_;
};

}  // namespace chunkmap_desktop
