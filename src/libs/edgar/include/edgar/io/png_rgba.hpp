#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace edgar::io {

/// Decoded 8-bit RGBA image (`stb_image`).
struct PngRgbaBuffer {
    int width = 0;
    int height = 0;
    int channels_in_file = 0;
    std::vector<std::uint8_t> rgba;
};

/// Loads a PNG (or other formats supported by stb_image) into RGBA8. Returns nullopt on failure.
std::optional<PngRgbaBuffer> load_image_rgba(const std::string& path);

} // namespace edgar::io
