#pragma once

#include <fstream>
#include <string>
#include <type_traits>

#include <nlohmann/json.hpp>

#include "edgar/generator/grid2d/layout_grid2d.hpp"

namespace edgar::io {

template <typename TRoom>
nlohmann::json layout_to_json(const generator::grid2d::LayoutGrid2D<TRoom>& layout) {
    nlohmann::json j;
    j["rooms"] = nlohmann::json::array();
    for (const auto& r : layout.rooms) {
        nlohmann::json jr;
        jr["room"] = r.room;
        jr["position"] = {{"x", r.position.x}, {"y", r.position.y}};
        jr["is_corridor"] = r.is_corridor;
        jr["transformation"] = static_cast<int>(r.transformation);
        jr["outline"] = nlohmann::json::array();
        for (const auto& p : r.outline.points()) {
            jr["outline"].push_back({{"x", p.x}, {"y", p.y}});
        }
        jr["room_template_name"] = r.room_template.name();
        j["rooms"].push_back(std::move(jr));
    }
    return j;
}

template <typename TRoom>
void layout_save_json(const generator::grid2d::LayoutGrid2D<TRoom>& layout, const std::string& path) {
    std::ofstream f(path);
    f << layout_to_json(layout).dump(2);
}

} // namespace edgar::io
