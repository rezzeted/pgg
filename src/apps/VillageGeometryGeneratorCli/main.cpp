// VillageGeometryGeneratorCli: Layout IR JSON -> PGG building assets ->
// merged OBJ (S6, docs/village_topology_generator/requirements.md §10).
//
// Usage:
//   VillageGeometryGeneratorCli <layout_ir.json> -o out.obj [--assets DIR] [--seed N]
//   VillageGeometryGeneratorCli --selftest   (topology pipeline + geometry, no IO)

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "pgg/src/eval/obj_export.h"

#include "village_geometry.h"

// selftest runs the topology pipeline in-memory.
#include "analysis.h"
#include "anchors.h"
#include "layout_ir.h"
#include "parcels.h"
#include "placement.h"
#include "roads.h"
#include "terrain.h"

namespace {

std::string find_content_pgg() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::current_path(ec);
    for (int depth = 0; depth < 12 && !dir.empty(); ++depth) {
        const fs::path candidate = dir / "content" / "pgg";
        if (fs::is_directory(candidate / "lib", ec) &&
            fs::is_directory(candidate / "Village" / "buildings", ec)) {
            return candidate.string();
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return "content/pgg";
}

nlohmann::json selftest_ir() {
    using namespace village_topology_generator;
    TerrainParams tp;
    tp.width = 64;
    tp.height = 64;
    tp.seed = 11;
    tp.water_level = 9.0;
    const Terrain t = generate_terrain(tp);
    const Analysis a = analyze_terrain(t);

    const auto center = pick_center(a);
    std::vector<Anchor> anchors = {
        {AnchorKind::center, center, a.attractiveness_at(center.first, center.second)}};
    std::vector<std::pair<int, int>> road_targets;
    if (const auto ws = pick_water_site(t, a, center)) {
        anchors.push_back({AnchorKind::water_site, *ws, a.attractiveness_at(ws->first, ws->second)});
        road_targets.push_back(*ws);
    }
    for (const auto& an : pick_edge_anchors(t, a)) {
        anchors.push_back(an);
        road_targets.push_back(an.cell);
    }
    const auto network = grow_skeleton(a, center, road_targets);
    const auto parcels = grow_parcels(a, network);

    Roster roster;
    roster.push_back({"mill", {SiteReq::near_water}, {}});
    roster.push_back({"bakery", {}, {"mill"}});
    roster.push_back({"tavern", {SiteReq::near_center}, {}});
    roster.push_back({"well", {SiteReq::near_center}, {}});
    for (int i = 0; i < 6; ++i) roster.push_back({"house", {}, {}});
    const auto placement = place_roster(t, a, parcels, roster, center);
    return build_layout_ir(tp, network, parcels, placement, anchors);
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string ir_path;
    std::string out_path;
    std::string scene_path;
    std::string assets_dir;
    uint64_t seed = 1;
    bool selftest = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--selftest") {
            selftest = true;
        } else if (a == "-o" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (a == "--emit-pgg" && i + 1 < argc) {
            scene_path = argv[++i];
        } else if (a == "--assets" && i + 1 < argc) {
            assets_dir = argv[++i];
        } else if (a == "--seed" && i + 1 < argc) {
            seed = std::stoull(argv[++i]);
        } else if (!a.empty() && a[0] != '-') {
            ir_path = a;
        }
    }

    nlohmann::json ir;
    if (selftest) {
        ir = selftest_ir();
    } else {
        if (ir_path.empty()) {
            std::fprintf(stderr, "usage: VillageGeometryGeneratorCli <layout_ir.json> -o out.obj\n");
            return 1;
        }
        std::ifstream in(ir_path);
        if (!in) {
            std::fprintf(stderr, "cannot open %s\n", ir_path.c_str());
            return 1;
        }
        in >> ir;
        const std::string format = ir.value("format", "");
        if (format != village_topology_generator::kLayoutFormat) {
            std::fprintf(stderr, "%s: unsupported format '%s' (expected %s)\n", ir_path.c_str(),
                         format.c_str(), village_topology_generator::kLayoutFormat);
            return 1;
        }
    }

    const std::string content_pgg = find_content_pgg();
    if (assets_dir.empty()) assets_dir = content_pgg + "/Village/buildings";

    if (!scene_path.empty()) {
        const std::string scene = village_geometry_generator::emit_scene_pgg(ir, seed);
        if (scene.empty()) {
            std::fprintf(stderr, "no buildings in the IR; scene not written\n");
            return 1;
        }
        std::ofstream(scene_path) << scene;
        std::printf("wrote %s (run: PggTool run %s --lib %s)\n", scene_path.c_str(),
                    scene_path.c_str(), content_pgg.c_str());
    }

    village_geometry_generator::GeomParams params;
    params.assets_dir = assets_dir;
    params.import_roots = {content_pgg};
    params.seed = seed;

    const auto result = village_geometry_generator::generate_geometry(ir, params);
    for (const auto& d : result.diagnostics) {
        std::fprintf(stderr, "  %s\n", d.c_str());
    }
    std::printf("buildings: %d/%d built\n", result.buildings_built, result.buildings_total);
    if (!result.mesh) {
        std::fprintf(stderr, "no geometry produced\n");
        return 1;
    }
    std::printf("mesh: %zu points, %zu faces\n", result.mesh->pointCount(), result.mesh->faceCount());

    if (!out_path.empty()) {
        std::string err;
        if (!pgg::writeObj(out_path, *result.mesh, &err)) {
            std::fprintf(stderr, "OBJ export failed: %s\n", err.c_str());
            return 1;
        }
        std::printf("wrote %s\n", out_path.c_str());
    }
    return 0;
}
