#include "edgar/io/dungeon_drawer.hpp"

#include <spdlog/spdlog.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace edgar::io {

void write_png_rgba(const std::string& path, int w, int h, const std::vector<std::uint8_t>& rgba) {
    if (!stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4)) {
        spdlog::error("write_png_rgba failed: {}", path);
    }
}

} // namespace edgar::io
