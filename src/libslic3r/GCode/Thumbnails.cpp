///|/ Copyright (c) Prusa Research 2022 Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Thumbnails.hpp"
#include "../miniz_extension.hpp"

#include <qoi/qoi.h>
#include <jpeglib.h>
#include <jerror.h>

namespace Slic3r::GCodeThumbnails {

using namespace std::literals;

struct CompressedPNG : CompressedImageBuffer
{
    ~CompressedPNG() override { if (data) mz_free(data); }
    std::string_view tag() const override { return "thumbnail"sv; }
};

struct CompressedJPG : CompressedImageBuffer
{
    ~CompressedJPG() override { free(data); }
    std::string_view tag() const override { return "thumbnail_JPG"sv; }
};

struct CompressedQOI : CompressedImageBuffer
{
    ~CompressedQOI() override { free(data); }
    std::string_view tag() const override { return "thumbnail_QOI"sv; }
};

struct CompressedBIQU : CompressedImageBuffer
{
    ~CompressedBIQU() override { free(data); }
    std::string_view tag() const override { return "thumbnail_BIQU"sv; }
};

std::unique_ptr<CompressedImageBuffer> compress_thumbnail_png(const ThumbnailData &data)
{
    auto out = std::make_unique<CompressedPNG>();
    out->data = tdefl_write_image_to_png_file_in_memory_ex((const void*)data.pixels.data(), data.width, data.height, 4, &out->size, MZ_DEFAULT_LEVEL, 1);
    return out;
}

std::unique_ptr<CompressedImageBuffer> compress_thumbnail_jpg(const ThumbnailData& data)
{
    // Take vector of RGBA pixels and flip the image vertically
    std::vector<unsigned char> rgba_pixels(data.pixels.size());
    const unsigned int row_size = data.width * 4;
    for (unsigned int y = 0; y < data.height; ++y) {
        ::memcpy(rgba_pixels.data() + (data.height - y - 1) * row_size, data.pixels.data() + y * row_size, row_size);
    }

    // Store pointers to scanlines start for later use
    std::vector<unsigned char*> rows_ptrs;
    rows_ptrs.reserve(data.height);
    for (unsigned int y = 0; y < data.height; ++y) {
        rows_ptrs.emplace_back(&rgba_pixels[y * row_size]);
    }

    std::vector<unsigned char> compressed_data(data.pixels.size());
    unsigned char* compressed_data_ptr = compressed_data.data();
    unsigned long compressed_data_size = data.pixels.size();

    jpeg_error_mgr err;
    jpeg_compress_struct info;
    info.err = jpeg_std_error(&err);
    jpeg_create_compress(&info);
    jpeg_mem_dest(&info, &compressed_data_ptr, &compressed_data_size);

    info.image_width = data.width;
    info.image_height = data.height;
    info.input_components = 4;
    info.in_color_space = JCS_EXT_RGBA;

    jpeg_set_defaults(&info);
    jpeg_set_quality(&info, 85, TRUE);
    jpeg_start_compress(&info, TRUE);

    jpeg_write_scanlines(&info, rows_ptrs.data(), data.height);
    jpeg_finish_compress(&info);
    jpeg_destroy_compress(&info);

    // FIXME -> Add error checking

    auto out = std::make_unique<CompressedJPG>();
    out->data = malloc(compressed_data_size);
    out->size = size_t(compressed_data_size);
    ::memcpy(out->data, (const void*)compressed_data.data(), out->size);
    return out;
}

std::unique_ptr<CompressedImageBuffer> compress_thumbnail_qoi(const ThumbnailData &data)
{
    qoi_desc desc;
    desc.width      = data.width;
    desc.height     = data.height;
    desc.channels   = 4;
    desc.colorspace = QOI_SRGB;

    // Take vector of RGBA pixels and flip the image vertically
    std::vector<uint8_t> rgba_pixels(data.pixels.size() * 4);
    size_t row_size = data.width * 4;
    for (size_t y = 0; y < data.height; ++ y)
        memcpy(rgba_pixels.data() + (data.height - y - 1) * row_size, data.pixels.data() + y * row_size, row_size);

    auto out = std::make_unique<CompressedQOI>();
    int  size;
    out->data = qoi_encode((const void*)rgba_pixels.data(), &desc, &size);
    out->size = size;
    return out;
}

std::unique_ptr<CompressedImageBuffer> compress_thumbnail_biqu(const ThumbnailData &data) {

    // Take vector of RGBA pixels and flip the image vertically
    std::vector<unsigned char> rgba_pixels(data.pixels.size());
    const unsigned int row_size = data.width * 4;
    for (unsigned int y = 0; y < data.height; ++y) {
        ::memcpy(rgba_pixels.data() + (data.height - y - 1) * row_size, data.pixels.data() + y * row_size, row_size);
    }

    auto out = std::make_unique<CompressedBIQU>();

    out->size = data.height * (2 + data.width * 4) + 1;
    out->data = malloc(out->size);

    std::stringstream out_data;
    typedef struct {unsigned char r, g, b, a;} pixel;
    pixel px;
    for (int ypos = 0; ypos < data.height; ypos++) {
        std::stringstream qrgb;
        qrgb << ";";
        for (int xpos = 0; xpos < row_size; xpos+=4) {
            px.r = rgba_pixels[ypos * row_size + xpos];
            px.g = rgba_pixels[ypos * row_size + xpos + 1];
            px.b = rgba_pixels[ypos * row_size + xpos + 2];
            px.a = rgba_pixels[ypos * row_size + xpos + 3];

            // Uses modified algorithm from PyQt5/Qt (the native BTT plugin for Cura uses this)
            unsigned int QRgb = ((px.a & 0xffu) << 24) | ((px.r & 0xffu) << 16) | ((px.g & 0xffu) << 8) | (px.b & 0xffu);
            unsigned int t = (QRgb & 0xff00ff) * px.a;
            t = (t + ((t >> 8) & 0xff00ff) + 0x800080) >> 8;
            t &= 0xff00ff;

            QRgb = ((QRgb >> 8) & 0xff) * px.a;
            QRgb = (QRgb + ((QRgb >> 8) & 0xff) + 0x80);
            QRgb &= 0xff00;
            QRgb = QRgb | t | (px.a << 24);

            auto dummy = rjust(get_hex(((QRgb & 0x00F80000) >> 8 ) | ((QRgb & 0x0000FC00) >> 5 ) | ((QRgb & 0x000000F8) >> 3 )), 4, '0');
            if (dummy == "0020" || dummy == "0841" || dummy == "0861")
                dummy = "0000";
            qrgb << dummy;
        }
        out_data << qrgb.str() << std::endl;
        qrgb.clear();
    }
    ::memcpy(out->data, (const void*) out_data.str().c_str(), out->size);
    return out;
}

std::string get_hex(const unsigned int input) {
    std::stringstream stream;
    stream << std::hex << input;
    return stream.str();
}

std::string rjust(std::string input, unsigned int width, char fill_char) {
    std::stringstream stream;
    stream.fill(fill_char);
    stream.width(width);
    stream << input;
    return stream.str();
}

std::unique_ptr<CompressedImageBuffer> compress_thumbnail(const ThumbnailData &data, GCodeThumbnailsFormat format)
{
    switch (format) {
    case GCodeThumbnailsFormat::PNG:
    default:
        return compress_thumbnail_png(data);
    case GCodeThumbnailsFormat::JPG:
        return compress_thumbnail_jpg(data);
    case GCodeThumbnailsFormat::QOI:
        return compress_thumbnail_qoi(data);
    case GCodeThumbnailsFormat::BIQU:
        return compress_thumbnail_biqu(data);
    }
}

} // namespace Slic3r::GCodeThumbnails