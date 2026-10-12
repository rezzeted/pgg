#include "pch.h"

#include "layout_ir.h"

#include <cstdio>

namespace village_topology_generator {
namespace {

using nlohmann::json;

json cell_meters(std::pair<int, int> c, double cell_size) {
    return json::array({(c.first + 0.5) * cell_size, (c.second + 0.5) * cell_size});
}

// 4 polygon corners (meters) of the rectangular lot: the corner cells of the
// front and back rows, expanded by half a cell along the run (a) and the
// depth (q) directions.
json parcel_polygon(const Parcel& p, double cell_size) {
    const size_t f = p.front.size();
    const size_t depth = p.cells.size() / f;
    const auto c00 = p.cells[0];
    const auto c0f = p.cells[f - 1];
    const auto cd0 = p.cells[(depth - 1) * f];
    const auto cdf = p.cells.back();

    std::pair<double, double> a{1.0, 0.0};  // along the front run
    if (f >= 2) {
        a = {static_cast<double>(p.cells[1].first - c00.first),
             static_cast<double>(p.cells[1].second - c00.second)};
    } else if (depth >= 2) {
        const auto q0 = p.cells[f];
        a = {static_cast<double>(c00.second - q0.second),
             static_cast<double>(q0.first - c00.first)};  // a = q rotated +90
    }
    std::pair<double, double> q{-a.second, a.first};  // depth direction
    if (depth >= 2) {
        q = {static_cast<double>(p.cells[f].first - c00.first),
             static_cast<double>(p.cells[f].second - c00.second)};
    }

    auto corner = [&](std::pair<int, int> c, double sa, double sq) {
        return json::array({(c.first + 0.5 + sa * a.first + sq * q.first) * cell_size,
                            (c.second + 0.5 + sa * a.second + sq * q.second) * cell_size});
    };
    return json::array({corner(c00, -0.5, -0.5), corner(c0f, 0.5, -0.5),
                        corner(cdf, 0.5, 0.5), corner(cd0, -0.5, 0.5)});
}

const char* anchor_kind_name(AnchorKind kind) {
    switch (kind) {
        case AnchorKind::center: return "center";
        case AnchorKind::water_site: return "water_site";
        case AnchorKind::edge: return "edge";
    }
    return "unknown";
}

}  // namespace

json build_layout_ir(const TerrainParams& terrain_params,
                     const RoadNetwork& network,
                     const std::vector<Parcel>& parcels,
                     const PlacementResult& placement,
                     const std::vector<Anchor>& anchors) {
    const double cs = terrain_params.cell_size;
    json ir;
    ir["format"] = kLayoutFormat;
    ir["terrain"] = {
        {"width", terrain_params.width},
        {"height", terrain_params.height},
        {"cell_size", terrain_params.cell_size},
        {"water_level", terrain_params.water_level},
        {"seed", terrain_params.seed},
        {"octaves", terrain_params.octaves},
        {"frequency", terrain_params.frequency},
        {"amplitude", terrain_params.amplitude},
        {"forest_threshold", terrain_params.forest_threshold},
    };

    json roads = json::array();
    for (const auto& r : network.roads) {
        json cells = json::array();
        for (const auto& c : r.cells) cells.push_back(cell_meters(c, cs));
        roads.push_back({{"class", "street"}, {"cells", std::move(cells)}});
    }
    ir["roads"] = std::move(roads);

    json jparcels = json::array();
    for (size_t i = 0; i < parcels.size(); ++i) {
        json front = json::array();
        for (const auto& c : parcels[i].front) front.push_back(cell_meters(c, cs));
        jparcels.push_back({
            {"id", static_cast<int>(i)},
            {"road", parcels[i].road_index},
            {"side", parcels[i].side},
            {"polygon", parcel_polygon(parcels[i], cs)},
            {"front", std::move(front)},
        });
    }
    ir["parcels"] = std::move(jparcels);

    json buildings = json::array();
    for (size_t i = 0; i < placement.buildings.size(); ++i) {
        const auto& b = placement.buildings[i];
        if (b.parcel_index < 0) continue;  // unplaced items are not in the IR
        buildings.push_back({
            {"id", static_cast<int>(i)},
            {"type", b.type},
            {"parcel", b.parcel_index},
            {"position", cell_meters(b.cell, cs)},
            {"facing", json::array({b.facing.first, b.facing.second})},
        });
    }
    ir["buildings"] = std::move(buildings);

    json janchors = json::array();
    for (const auto& a : anchors) {
        janchors.push_back({
            {"kind", anchor_kind_name(a.kind)},
            {"cell", json::array({a.cell.first, a.cell.second})},
            {"score", a.score},
        });
    }
    ir["anchors"] = std::move(janchors);
    ir["zones"] = json::array();
    return ir;
}

std::string canonical_layout_json(const json& ir) {
    return ir.dump();  // object keys are sorted by default
}

uint64_t layout_fingerprint(const json& ir) {
    const std::string text = canonical_layout_json(ir);
    uint64_t h = 1469598103934665603ull;  // FNV-1a 64 offset basis
    for (const unsigned char ch : text) {
        h ^= ch;
        h *= 1099511628211ull;
    }
    return h;
}

std::string fingerprint_hex(uint64_t fingerprint) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(fingerprint));
    return buf;
}

}  // namespace village_topology_generator
