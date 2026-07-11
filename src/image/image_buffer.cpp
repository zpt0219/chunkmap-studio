#include "image/image_buffer.h"

#include "io/atomic_file.h"

#include <limits>
#include <algorithm>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <stb_image.h>
#include <stb_image_write.h>
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace chunkmap {

namespace {

Result<std::size_t> rgba_size(int width, int height) {
    if (width <= 0 || height <= 0) {
        return Result<std::size_t>::failure("invalid_image_size", "Image dimensions must be positive.");
    }
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h ||
        w * h > std::numeric_limits<std::size_t>::max() / 4U) {
        return Result<std::size_t>::failure("image_too_large", "Image pixel buffer is too large.");
    }
    return Result<std::size_t>::success(w * h * 4U);
}

void png_write_callback(void* context, void* data, int size) {
    auto* output = static_cast<std::vector<std::uint8_t>*>(context);
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    output->insert(output->end(), bytes, bytes + size);
}

}  // namespace

ImageBuffer::ImageBuffer(int width, int height) : width_(width), height_(height) {
    auto size = rgba_size(width, height);
    if (size) rgba_.resize(size.value(), 0U);
}

Result<ImageBuffer> ImageBuffer::load(const std::filesystem::path& path) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        const char* reason = stbi_failure_reason();
        return Result<ImageBuffer>::failure(
            "image_decode_failed",
            "Unable to decode image " + path.string() +
                (reason ? ": " + std::string(reason) : std::string{}));
    }

    auto size = rgba_size(width, height);
    if (!size) {
        stbi_image_free(pixels);
        return Result<ImageBuffer>::failure(size.error().code, size.error().message);
    }

    ImageBuffer image;
    image.width_ = width;
    image.height_ = height;
    image.rgba_.assign(pixels, pixels + size.value());
    stbi_image_free(pixels);
    return Result<ImageBuffer>::success(std::move(image));
}

Result<std::vector<std::uint8_t>> ImageBuffer::encode_png() const {
    if (empty() || width_ <= 0 || height_ <= 0) {
        return Result<std::vector<std::uint8_t>>::failure(
            "empty_image", "Cannot encode an empty image.");
    }

    if (width_ > std::numeric_limits<int>::max() / 4) {
        return Result<std::vector<std::uint8_t>>::failure(
            "image_too_large", "Image row stride is too large.");
    }

    std::vector<std::uint8_t> encoded;
    const int ok = stbi_write_png_to_func(
        png_write_callback,
        &encoded,
        width_,
        height_,
        4,
        rgba_.data(),
        width_ * 4);
    if (ok == 0) {
        return Result<std::vector<std::uint8_t>>::failure(
            "image_encode_failed", "Unable to encode PNG image.");
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(encoded));
}

Result<void> ImageBuffer::save_png(const std::filesystem::path& path) const {
    auto encoded = encode_png();
    if (!encoded) return Result<void>::failure(encoded.error().code, encoded.error().message);
    return atomic_file::write_binary(path, encoded.value());
}

Result<ImageBuffer> ImageBuffer::crop(Rect rect) const {
    if (rect.x < 0 || rect.y < 0 || rect.width <= 0 || rect.height <= 0 ||
        rect.x > width_ - rect.width || rect.y > height_ - rect.height) {
        return Result<ImageBuffer>::failure("invalid_crop", "Crop rectangle is outside the image.");
    }

    ImageBuffer result(rect.width, rect.height);
    for (int y = 0; y < rect.height; ++y) {
        const auto* source = pixel(rect.x, rect.y + y);
        auto* destination = result.pixel(0, y);
        std::copy_n(source, static_cast<std::size_t>(rect.width) * 4U, destination);
    }
    return Result<ImageBuffer>::success(std::move(result));
}

Result<void> ImageBuffer::blit(const ImageBuffer& source,
                               Rect source_rect,
                               Point destination) {
    if (source_rect.x < 0 || source_rect.y < 0 || source_rect.width <= 0 ||
        source_rect.height <= 0 ||
        source_rect.x > source.width_ - source_rect.width ||
        source_rect.y > source.height_ - source_rect.height ||
        destination.x < 0 || destination.y < 0 ||
        destination.x > width_ - source_rect.width ||
        destination.y > height_ - source_rect.height) {
        return Result<void>::failure("invalid_blit", "Blit rectangle is outside an image.");
    }

    for (int y = 0; y < source_rect.height; ++y) {
        const auto* source_row = source.pixel(source_rect.x, source_rect.y + y);
        auto* destination_row = pixel(destination.x, destination.y + y);
        std::copy_n(
            source_row, static_cast<std::size_t>(source_rect.width) * 4U, destination_row);
    }
    return Result<void>::success();
}

std::uint8_t* ImageBuffer::pixel(int x, int y) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_ || rgba_.empty()) return nullptr;
    return rgba_.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                           static_cast<std::size_t>(x)) * 4U;
}

const std::uint8_t* ImageBuffer::pixel(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_ || rgba_.empty()) return nullptr;
    return rgba_.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                           static_cast<std::size_t>(x)) * 4U;
}

}  // namespace chunkmap
