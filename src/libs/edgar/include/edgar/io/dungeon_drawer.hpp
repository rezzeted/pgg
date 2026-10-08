#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "edgar/generator/grid2d/layout_grid2d.hpp"
#include "edgar/io/dungeon_drawer_options.hpp"

namespace edgar::io {

/// Writes RGBA8888 buffer to PNG (implementation in dungeon_drawer.cpp, uses stb_image_write).
void write_png_rgba(const std::string& path, int w, int h, const std::vector<std::uint8_t>& rgba);

template <typename TRoom>
class DungeonDrawer {
public:
    void draw_layout_and_save(const generator::grid2d::LayoutGrid2D<TRoom>& layout, const std::string& path,
                              const DungeonDrawerOptions& options = {}) const;
};

} // namespace edgar::io

#include "edgar/io/dungeon_drawer.inl"
