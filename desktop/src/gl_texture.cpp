#include "gl_texture.h"

#include "image/image_buffer.h"

#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <GLFW/glfw3.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <system_error>

namespace chunkmap_desktop {

int maximum_texture_size() {
    int result = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &result);
    return result;
}

GlTexture::~GlTexture() {
    reset();
}

chunkmap::Result<void> GlTexture::load(const std::filesystem::path& path) {
    auto image = chunkmap::ImageBuffer::load(path);
    if (!image) return chunkmap::Result<void>::failure(image.error().code, image.error().message);
    if (id_ == 0) glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.value().width(), image.value().height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, image.value().rgba().data());
    glBindTexture(GL_TEXTURE_2D, 0);
    width_ = image.value().width();
    height_ = image.value().height();
    return chunkmap::Result<void>::success();
}

void GlTexture::reset() {
    if (id_ != 0) glDeleteTextures(1, &id_);
    id_ = 0;
    width_ = 0;
    height_ = 0;
}

GlTexture* TextureCache::get(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) return nullptr;
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) return nullptr;
    auto& entry = entries_[path];
    if (!entry.texture || entry.modified != modified) {
        auto texture = std::make_unique<GlTexture>();
        auto loaded = texture->load(path);
        if (!loaded) {
            last_error_ = loaded.error().message;
            entries_.erase(path);
            return nullptr;
        }
        entry.texture = std::move(texture);
        entry.modified = modified;
        last_error_.clear();
    }
    return entry.texture.get();
}

void TextureCache::invalidate(const std::filesystem::path& path) {
    entries_.erase(path);
}

void TextureCache::clear() {
    entries_.clear();
    last_error_.clear();
}

}  // namespace chunkmap_desktop
