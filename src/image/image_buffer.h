#pragma once

#include "core/result.h"
#include "image/image_geometry.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace chunkmap {

class ImageBuffer {
public:
    ImageBuffer() = default;
    ImageBuffer(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }
    bool empty() const { return rgba_.empty(); }

    std::vector<std::uint8_t>& rgba() { return rgba_; }
    const std::vector<std::uint8_t>& rgba() const { return rgba_; }

    static Result<ImageBuffer> load(const std::filesystem::path& path);
    Result<std::vector<std::uint8_t>> encode_png() const;
    Result<void> save_png(const std::filesystem::path& path) const;

    Result<ImageBuffer> crop(Rect rect) const;
    Result<void> blit(const ImageBuffer& source, Rect source_rect, Point destination);

    std::uint8_t* pixel(int x, int y);
    const std::uint8_t* pixel(int x, int y) const;

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> rgba_;
};

}  // namespace chunkmap
