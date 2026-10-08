#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dungeon_topology_generator/generator/grid2d/layout_grid2d.hpp"
#include "dungeon_topology_generator/io/dungeon_drawer_options.hpp"

namespace dungeon_topology_generator::io {

/// Writes RGBA8888 buffer to PNG (implementation in dungeon_drawer.cpp, uses stb_image_write).
void write_png_rgba(const std::string& path, int w, int h, const std::vector<std::uint8_t>& rgba);

template <typename TRoom>
class DungeonDrawer {
public:
    void draw_layout_and_save(const generator::grid2d::LayoutGrid2D<TRoom>& layout, const std::string& path,
                              const DungeonDrawerOptions& options = {}) const;
};

} // namespace dungeon_topology_generator::io

#include "dungeon_topology_generator/io/dungeon_drawer.inl"
