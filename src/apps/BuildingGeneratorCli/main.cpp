// BuildingGeneratorCli (B0): Building IR JSON -> PGG slot assets -> merged OBJ
// + anchors.json + section scene + checks (docs/building_generator/
// requirements.md sections 11, 14.2, 15).
//
// Usage:
//   BuildingGeneratorCli fill <ir.json> -o <dir> [--assets DIR] [--seed N]
//       [--emit-pgg <scene.pgg>]     -> building.obj, anchors.json
//   BuildingGeneratorCli check <ir.json> [--assets DIR] [--seed N]
//       -> fills in-memory, runs the section 14.2 checks, nonzero on failure
//   BuildingGeneratorCli --selftest
//       -> frozen house (content/building/frozen/house_b0.ir.json): fill +
//          check + OBJ, no external inputs besides the repo content

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "pgg/src/eval/obj_export.h"

#include "building_geometry.h"

namespace {

// Walk up from cwd until the repo content/ directory (both Building assets
// and the frozen IR live under it).
std::string find_content_dir() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::current_path(ec);
    for (int depth = 0; depth < 12 && !dir.empty(); ++depth) {
        const fs::path candidate = dir / "content";
        if (fs::is_directory(candidate / "pgg" / "Building", ec) &&
            fs::is_directory(candidate / "building" / "frozen", ec)) {
            return candidate.string();
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return "content";
}

int do_fill(const std::string& ir_path, const std::string& out_dir, const std::string& scene_path,
            const std::string& assets_dir, uint64_t seed) {
    namespace bg = building_geometry_generator;
    building_topology_generator::Ir ir;
    std::string err;
    if (!building_topology_generator::load_ir_file(ir_path, ir, err)) {
        std::fprintf(stderr, "%s: %s\n", ir_path.c_str(), err.c_str());
        return 1;
    }

    bg::FillParams params;
    params.assets_dir = assets_dir;
    params.import_roots = {assets_dir + "/.."};
    params.seed = seed;

    const bg::FillResult fill = bg::fill_building(ir, params);
    for (const auto& d : fill.diagnostics) std::fprintf(stderr, "  %s\n", d.c_str());
    std::printf("units: %d/%d built\n", fill.units_built, fill.units_total);
    if (!fill.mesh) {
        std::fprintf(stderr, "no geometry produced\n");
        return 1;
    }
    std::printf("mesh: %zu points, %zu faces\n", fill.mesh->pointCount(), fill.mesh->faceCount());

    if (!out_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(out_dir, ec);
        const std::string obj_path = out_dir + "/building.obj";
        if (!pgg::writeObj(obj_path, *fill.mesh, &err)) {
            std::fprintf(stderr, "OBJ export failed: %s\n", err.c_str());
            return 1;
        }
        std::printf("wrote %s\n", obj_path.c_str());
        const std::string anchors_path = out_dir + "/anchors.json";
        std::ofstream(anchors_path) << bg::anchors_json(ir).dump(2) << "\n";
        std::printf("wrote %s\n", anchors_path.c_str());
    }
    if (!scene_path.empty()) {
        std::ofstream(scene_path) << bg::emit_scene_pgg(ir, params);
        std::printf("wrote %s (open in PggViewer; param cut_y = storey section)\n",
                    scene_path.c_str());
    }
    return 0;
}

int do_check(const std::string& ir_path, const std::string& assets_dir, uint64_t seed) {
    namespace bg = building_geometry_generator;
    building_topology_generator::Ir ir;
    std::string err;
    if (!building_topology_generator::load_ir_file(ir_path, ir, err)) {
        std::fprintf(stderr, "%s: %s\n", ir_path.c_str(), err.c_str());
        return 1;
    }
    bg::FillParams params;
    params.assets_dir = assets_dir;
    params.import_roots = {assets_dir + "/.."};
    params.seed = seed;
    const bg::FillResult fill = bg::fill_building(ir, params);
    const bg::CheckResult check = bg::check_building(ir, fill);
    for (const auto& n : check.notes) std::printf("note: %s\n", n.c_str());
    for (const auto& f : check.failures) std::printf("FAIL: %s\n", f.c_str());
    for (const auto& d : fill.diagnostics) std::printf("diag: %s\n", d.c_str());
    std::printf("check: %s (%zu failure(s), %d/%d units built)\n", check.ok() ? "OK" : "FAILED",
                check.failures.size(), fill.units_built, fill.units_total);
    return check.ok() && fill.units_built == fill.units_total ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string command;
    std::string ir_path;
    std::string out_dir;
    std::string scene_path;
    std::string assets_dir;
    uint64_t seed = 1;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--selftest") {
            command = "selftest";
        } else if (a == "-o" && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (a == "--emit-pgg" && i + 1 < argc) {
            scene_path = argv[++i];
        } else if (a == "--assets" && i + 1 < argc) {
            assets_dir = argv[++i];
        } else if (a == "--seed" && i + 1 < argc) {
            seed = std::stoull(argv[++i]);
        } else if ((a == "fill" || a == "check") && command.empty()) {
            command = a;
        } else if (!a.empty() && a[0] != '-') {
            ir_path = a;
        }
    }

    const std::string content = find_content_dir();
    if (assets_dir.empty()) assets_dir = content + "/pgg/Building";

    if (command == "selftest") {
        const std::string frozen = content + "/building/frozen/house_b0.ir.json";
        std::printf("selftest: %s\n", frozen.c_str());
        return do_check(frozen, assets_dir, seed);
    }
    if (command == "fill") {
        if (ir_path.empty() || out_dir.empty()) {
            std::fprintf(stderr, "usage: BuildingGeneratorCli fill <ir.json> -o <dir> "
                                 "[--assets DIR] [--seed N] [--emit-pgg scene.pgg]\n");
            return 1;
        }
        return do_fill(ir_path, out_dir, scene_path, assets_dir, seed);
    }
    if (command == "check") {
        if (ir_path.empty()) {
            std::fprintf(stderr, "usage: BuildingGeneratorCli check <ir.json> [--assets DIR]\n");
            return 1;
        }
        return do_check(ir_path, assets_dir, seed);
    }
    std::fprintf(stderr,
                 "usage: BuildingGeneratorCli fill <ir.json> -o <dir> [--emit-pgg scene.pgg]\n"
                 "       BuildingGeneratorCli check <ir.json>\n"
                 "       BuildingGeneratorCli --selftest\n");
    return 1;
}
