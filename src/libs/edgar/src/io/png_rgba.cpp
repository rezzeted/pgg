#include "edgar/io/png_rgba.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace edgar::io {

std::optional<PngRgbaBuffer> load_image_rgba(const std::string& path) {
    int w = 0;
    int h = 0;
    int ch = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data || w <= 0 || h <= 0) {
        if (data) {
            stbi_image_free(data);
        }
        return std::nullopt;
    }
    const std::size_t bytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
    PngRgbaBuffer out;
    out.width = w;
    out.height = h;
    out.channels_in_file = ch;
    out.rgba.assign(data, data + bytes);
    stbi_image_free(data);
    return out;
}

} // namespace edgar::io
