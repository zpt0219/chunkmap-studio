#include "image/png_stream_writer.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <string>

namespace chunkmap {

namespace {

std::array<std::uint8_t, 4> big_endian(std::uint32_t value) {
    return {static_cast<std::uint8_t>((value >> 24U) & 0xffU),
            static_cast<std::uint8_t>((value >> 16U) & 0xffU),
            static_cast<std::uint8_t>((value >> 8U) & 0xffU),
            static_cast<std::uint8_t>(value & 0xffU)};
}

}  // namespace

struct PngStreamWriter::Impl {
    Result<void> write_bytes(const void* data, std::size_t size) {
        if (size > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
            return Result<void>::failure("image_encode_failed", "PNG write is too large.");
        }
        output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!output) {
            return Result<void>::failure("export_write_failed", "Unable to write PNG output.");
        }
        return Result<void>::success();
    }

    Result<void> write_chunk(const char type[4], const std::uint8_t* data, std::size_t size) {
        if (size > std::numeric_limits<std::uint32_t>::max()) {
            return Result<void>::failure("image_encode_failed", "PNG chunk is too large.");
        }
        const auto length = big_endian(static_cast<std::uint32_t>(size));
        auto written = write_bytes(length.data(), length.size());
        if (!written) return written;
        written = write_bytes(type, 4U);
        if (!written) return written;
        if (size > 0U) {
            written = write_bytes(data, size);
            if (!written) return written;
        }
        uLong crc = crc32(0L, Z_NULL, 0);
        crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4U);
        if (size > 0U) crc = crc32(crc, data, static_cast<uInt>(size));
        const auto encoded_crc = big_endian(static_cast<std::uint32_t>(crc));
        return write_bytes(encoded_crc.data(), encoded_crc.size());
    }

    Result<void> deflate_input(const std::uint8_t* data, std::size_t size, int flush) {
        if (size > std::numeric_limits<uInt>::max()) {
            return Result<void>::failure("export_row_too_large", "PNG row exceeds zlib input limits.");
        }
        stream.next_in = const_cast<Bytef*>(data);
        stream.avail_in = static_cast<uInt>(size);
        int code = Z_OK;
        do {
            stream.next_out = compressed.data();
            stream.avail_out = static_cast<uInt>(compressed.size());
            code = deflate(&stream, flush);
            if (code != Z_OK && code != Z_STREAM_END) {
                return Result<void>::failure("image_encode_failed", "Unable to compress PNG data.");
            }
            const std::size_t produced = compressed.size() - stream.avail_out;
            if (produced > 0U) {
                auto written = write_chunk("IDAT", compressed.data(), produced);
                if (!written) return written;
            }
        } while (stream.avail_in > 0U || (flush == Z_FINISH && code != Z_STREAM_END));
        return Result<void>::success();
    }

    std::ofstream output;
    z_stream stream{};
    std::array<std::uint8_t, 64U * 1024U> compressed{};
    std::size_t row_bytes = 0;
    int height = 0;
    int rows_written = 0;
    bool deflate_initialized = false;
    bool finished = false;
};

PngStreamWriter::PngStreamWriter() : impl_(std::make_unique<Impl>()) {}

PngStreamWriter::~PngStreamWriter() {
    if (impl_->deflate_initialized) deflateEnd(&impl_->stream);
}

Result<void> PngStreamWriter::open(const std::filesystem::path& path, int width, int height) {
    if (width <= 0 || height <= 0 || impl_->output.is_open()) {
        return Result<void>::failure("image_encode_failed", "Invalid PNG stream dimensions or state.");
    }
    const std::size_t width_value = static_cast<std::size_t>(width);
    if (width_value > std::numeric_limits<std::size_t>::max() / 4U ||
        width_value * 4U > std::numeric_limits<uInt>::max()) {
        return Result<void>::failure("export_row_too_large", "PNG row exceeds supported limits.");
    }
    impl_->output.open(path, std::ios::binary | std::ios::trunc);
    if (!impl_->output) {
        return Result<void>::failure("export_write_failed", "Unable to open PNG output: " + path.string());
    }
    constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    auto written = impl_->write_bytes(signature.data(), signature.size());
    if (!written) return written;
    std::array<std::uint8_t, 13> header{};
    const auto encoded_width = big_endian(static_cast<std::uint32_t>(width));
    const auto encoded_height = big_endian(static_cast<std::uint32_t>(height));
    std::copy(encoded_width.begin(), encoded_width.end(), header.begin());
    std::copy(encoded_height.begin(), encoded_height.end(), header.begin() + 4);
    header[8] = 8;
    header[9] = 6;
    written = impl_->write_chunk("IHDR", header.data(), header.size());
    if (!written) return written;
    if (deflateInit(&impl_->stream, Z_DEFAULT_COMPRESSION) != Z_OK) {
        return Result<void>::failure("image_encode_failed", "Unable to initialize PNG compression.");
    }
    impl_->deflate_initialized = true;
    impl_->row_bytes = width_value * 4U;
    impl_->height = height;
    return Result<void>::success();
}

Result<void> PngStreamWriter::write_rows(const std::uint8_t* rgba,
                                         int row_count,
                                         std::size_t stride) {
    if (!impl_->deflate_initialized || impl_->finished || !rgba || row_count < 0 ||
        stride < impl_->row_bytes || row_count > impl_->height - impl_->rows_written) {
        return Result<void>::failure("image_encode_failed", "Invalid PNG row write.");
    }
    constexpr std::uint8_t filter = 0;
    for (int row = 0; row < row_count; ++row) {
        auto compressed = impl_->deflate_input(&filter, 1U, Z_NO_FLUSH);
        if (!compressed) return compressed;
        compressed = impl_->deflate_input(rgba + static_cast<std::size_t>(row) * stride,
                                          impl_->row_bytes, Z_NO_FLUSH);
        if (!compressed) return compressed;
        ++impl_->rows_written;
    }
    return Result<void>::success();
}

Result<void> PngStreamWriter::finish() {
    if (!impl_->deflate_initialized || impl_->finished || impl_->rows_written != impl_->height) {
        return Result<void>::failure("image_encode_failed", "PNG stream is incomplete.");
    }
    auto compressed = impl_->deflate_input(nullptr, 0U, Z_FINISH);
    if (!compressed) return compressed;
    deflateEnd(&impl_->stream);
    impl_->deflate_initialized = false;
    auto written = impl_->write_chunk("IEND", nullptr, 0U);
    if (!written) return written;
    impl_->output.close();
    if (!impl_->output) {
        return Result<void>::failure("export_write_failed", "Unable to finish PNG output.");
    }
    impl_->finished = true;
    return Result<void>::success();
}

}  // namespace chunkmap
