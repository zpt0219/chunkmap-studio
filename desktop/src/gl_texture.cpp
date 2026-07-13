#include "gl_texture.h"

#include "image/image_buffer.h"

#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <GLFW/glfw3.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <chrono>
#include <system_error>

namespace chunkmap_desktop {

GlTexture::~GlTexture() {
    reset();
}

chunkmap::Result<void> GlTexture::load(const std::filesystem::path& path) {
    auto image = chunkmap::ImageBuffer::load(path);
    if (!image) return chunkmap::Result<void>::failure(image.error().code, image.error().message);
    return load(image.value());
}

chunkmap::Result<void> GlTexture::load(const chunkmap::ImageBuffer& image) {
    if (id_ == 0) glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width(), image.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba().data());
    glBindTexture(GL_TEXTURE_2D, 0);
    width_ = image.width();
    height_ = image.height();
    return chunkmap::Result<void>::success();
}

void GlTexture::reset() {
    if (id_ != 0) glDeleteTextures(1, &id_);
    id_ = 0;
    width_ = 0;
    height_ = 0;
}

TextureCache::TextureCache() {
    const unsigned int cpu_count = std::thread::hardware_concurrency();
    const unsigned int worker_count = cpu_count > 1U ? cpu_count - 1U : 1U;
    loader_threads_.reserve(worker_count);
    for (unsigned int index = 0; index < worker_count; ++index) {
        loader_threads_.emplace_back(&TextureCache::loader_loop, this);
    }
}

TextureCache::~TextureCache() {
    {
        std::lock_guard<std::mutex> lock(loader_mutex_);
        loader_stopping_ = true;
        pending_loads_.clear();
    }
    loader_condition_.notify_all();
    for (auto& thread : loader_threads_) {
        if (thread.joinable()) thread.join();
    }
}

GlTexture* TextureCache::get(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) return nullptr;
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) return nullptr;
    auto& entry = entries_[path];
    if (entry.texture && entry.modified == modified) return entry.texture.get();
    entries_.erase(path);

    std::optional<DecodeResult> decoded;
    const LoadIdentity wanted{modified, generation_};
    {
        std::lock_guard<std::mutex> lock(loader_mutex_);
        auto completed = decoded_loads_.find(path);
        if (completed != decoded_loads_.end()) {
            if (completed->second.identity == wanted) {
                decoded = std::move(completed->second);
            }
            decoded_loads_.erase(completed);
        }
        if (!decoded) {
            auto requested = requested_loads_.find(path);
            if (requested == requested_loads_.end() ||
                !(requested->second == wanted)) {
                requested_loads_[path] = wanted;
                pending_loads_.push_back({path, wanted});
                loader_condition_.notify_one();
            }
        }
    }
    if (!decoded) return nullptr;

    LoadEvent event;
    event.path = path;
    event.decode_ms = decoded->decode_ms;
    if (!decoded->image) {
        event.error = decoded->error;
        last_error_ = event.error;
        load_events_.push_back(std::move(event));
        return nullptr;
    }
    auto texture = std::make_unique<GlTexture>();
    const auto upload_started = std::chrono::steady_clock::now();
    auto loaded = texture->load(*decoded->image);
    const auto upload_finished = std::chrono::steady_clock::now();
    event.upload_ms = std::chrono::duration<double, std::milli>(
        upload_finished - upload_started).count();
    if (!loaded) {
        event.error = loaded.error().message;
        last_error_ = event.error;
        load_events_.push_back(std::move(event));
        return nullptr;
    }
    event.success = true;
    load_events_.push_back(std::move(event));
    auto& loaded_entry = entries_[path];
    loaded_entry.texture = std::move(texture);
    loaded_entry.modified = modified;
    last_error_.clear();
    return loaded_entry.texture.get();
}

chunkmap::Result<void> TextureCache::put(const std::filesystem::path& path,
                                         const chunkmap::ImageBuffer& image) {
    forget_pending(path);
    auto texture = std::make_unique<GlTexture>();
    auto loaded = texture->load(image);
    if (!loaded) return loaded;
    std::error_code error;
    auto& entry = entries_[path];
    entry.texture = std::move(texture);
    entry.modified = std::filesystem::last_write_time(path, error);
    if (error) entry.modified = {};
    last_error_.clear();
    return chunkmap::Result<void>::success();
}

void TextureCache::invalidate(const std::filesystem::path& path) {
    entries_.erase(path);
    forget_pending(path);
}

void TextureCache::clear() {
    entries_.clear();
    last_error_.clear();
    std::lock_guard<std::mutex> lock(loader_mutex_);
    ++generation_;
    pending_loads_.clear();
    requested_loads_.clear();
    decoded_loads_.clear();
}

std::vector<TextureCache::LoadEvent> TextureCache::take_load_events() {
    std::vector<LoadEvent> result;
    result.swap(load_events_);
    return result;
}

void TextureCache::forget_pending(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(loader_mutex_);
    requested_loads_.erase(path);
    decoded_loads_.erase(path);
}

void TextureCache::loader_loop() {
    while (true) {
        LoadTask task;
        {
            std::unique_lock<std::mutex> lock(loader_mutex_);
            loader_condition_.wait(lock, [&] {
                return loader_stopping_ || !pending_loads_.empty();
            });
            if (pending_loads_.empty()) {
                if (loader_stopping_) return;
                continue;
            }
            task = std::move(pending_loads_.front());
            pending_loads_.pop_front();
        }

        DecodeResult decoded;
        decoded.path = task.path;
        decoded.identity = task.identity;
        const auto started = std::chrono::steady_clock::now();
        auto image = chunkmap::ImageBuffer::load(task.path);
        const auto finished = std::chrono::steady_clock::now();
        decoded.decode_ms = std::chrono::duration<double, std::milli>(finished - started).count();
        if (image) decoded.image = image.take_value();
        else decoded.error = image.error().message;

        std::lock_guard<std::mutex> lock(loader_mutex_);
        auto requested = requested_loads_.find(task.path);
        if (requested != requested_loads_.end() &&
            requested->second == task.identity) {
            decoded_loads_.insert_or_assign(task.path, std::move(decoded));
            requested_loads_.erase(requested);
        }
    }
}

}  // namespace chunkmap_desktop
