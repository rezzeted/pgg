// BuildingGeneratorCli: BuildingGenerator machine loop (docs/building_generator/
// requirements.md section 15).
//
// Usage:
//   BuildingGeneratorCli generate --program <json> --w <m> --d <m> -o <dir>
//       [--setback m] [--max-storeys n] [--seed N]
//       [--pin <pin.json> | --pin-from <ir.json> --pin massing,cores]
//       -> ir.json, report.txt, plans/storey_<k>.svg (B1: program + massing)
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
#include "massing.h"
#include "pin.h"
#include "plan_svg.h"
#include "program.h"

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

// generate: program -> G1 (validate, expand) -> G2 (massing; pinned massing
// is validated and used as-is, §4.5) -> ir.json + report.txt + SVG plans.
int do_generate(const std::string& program_path, const std::string& out_dir,
                const building_topology_generator::Site& site, uint64_t seed,
                const std::string& pin_path, const std::string& pin_from,
                const std::string& pin_what) {
    namespace bt = building_topology_generator;
    std::string err;

    bt::Program program;
    if (!bt::load_program_file(program_path, program, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    const bt::ExpandedProgram expanded = bt::expand_program(program);

    bt::Pin pin;
    std::string pin_desc;
    if (!pin_path.empty()) {
        if (!bt::load_pin_file(pin_path, pin, err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        pin_desc = pin_path;
    }
    if (!pin_from.empty()) {
        bt::Ir pin_ir;
        if (!bt::load_ir_file(pin_from, pin_ir, err)) {
            std::fprintf(stderr, "%s: %s\n", pin_from.c_str(), err.c_str());
            return 1;
        }
        if (!bt::pin_from_ir(pin_ir, pin_what.empty() ? "massing" : pin_what, pin, err)) {
            std::fprintf(stderr, "--pin-from %s: %s\n", pin_from.c_str(), err.c_str());
            return 1;
        }
        pin_desc = pin_from + " (" + (pin_what.empty() ? "massing" : pin_what) + ")";
    }

    bt::MassingParams mp;
    bt::MassingResult massing =
        pin.has_massing
            ? bt::massing_from_pin(expanded, site, mp, seed, pin.massing_volumes, pin_desc)
            : bt::generate_massing(expanded, site, mp, seed);
    if (!massing.ok) {
        std::fprintf(stderr, "generate: %s\n", massing.error.c_str());
        return 1;
    }

    bt::Ir ir;
    if (!bt::massing_to_ir(massing.chosen, expanded, mp, seed, ir, err)) {
        std::fprintf(stderr, "generate: %s\n", err.c_str());
        return 1;
    }

    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    std::filesystem::create_directories(out_dir + "/plans", ec);
    const std::string ir_out = out_dir + "/ir.json";
    if (!bt::save_ir_file(ir, ir_out, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::ofstream(out_dir + "/report.txt") << massing.report;
    for (size_t k = 0; k < ir.storeys.size(); ++k) {
        const std::string svg_path =
            out_dir + "/plans/storey_" + ir.storeys[k].id + ".svg";
        std::ofstream(svg_path) << bt::storey_plan_svg(ir, static_cast<int>(k), site);
    }

    std::printf("program: %s (%zu rooms, required %.1f m2)\n", expanded.type.c_str(),
                expanded.rooms.size(), expanded.required_area());
    std::printf("massing: %s\n", massing.chosen.shape.c_str());
    std::printf("wrote %s, report.txt, %zu plan(s)\n", ir_out.c_str(), ir.storeys.size());
    if (pin.has_cores)
        std::printf("note: pinned cores carried (%s), applied from B3 (G4)\n",
                    pin.cores_note.c_str());
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
    std::string program_path;
    std::string pin_path;
    std::string pin_from;
    std::string pin_what;
    building_topology_generator::Site site;
    site.w = 0.0;
    site.d = 0.0;
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
        } else if (a == "--program" && i + 1 < argc) {
            program_path = argv[++i];
        } else if (a == "--w" && i + 1 < argc) {
            site.w = std::stod(argv[++i]);
        } else if (a == "--d" && i + 1 < argc) {
            site.d = std::stod(argv[++i]);
        } else if (a == "--setback" && i + 1 < argc) {
            site.setback = std::stod(argv[++i]);
        } else if (a == "--max-storeys" && i + 1 < argc) {
            site.max_storeys = std::stoi(argv[++i]);
        } else if (a == "--pin" && i + 1 < argc) {
            // A bare selector (massing,cores) goes with --pin-from; anything
            // else is a pin file (section 4.5).
            const std::string v = argv[++i];
            if (v.find_first_not_of("abcdefghijklmnopqrstuvwxyz,") == std::string::npos)
                pin_what = v;
            else
                pin_path = v;
        } else if (a == "--pin-from" && i + 1 < argc) {
            pin_from = argv[++i];
        } else if ((a == "fill" || a == "check" || a == "generate") && command.empty()) {
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
    if (command == "generate") {
        if (program_path.empty() || site.w <= 0.0 || site.d <= 0.0 || out_dir.empty()) {
            std::fprintf(stderr, "usage: BuildingGeneratorCli generate --program <json> --w <m> "
                                 "--d <m> -o <dir> [--setback m] [--max-storeys n] [--seed N] "
                                 "[--pin pin.json | --pin-from ir.json --pin massing,cores]\n");
            return 1;
        }
        return do_generate(program_path, out_dir, site, seed, pin_path, pin_from, pin_what);
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
                 "usage: BuildingGeneratorCli generate --program <json> --w <m> --d <m> -o <dir>\n"
                 "       BuildingGeneratorCli fill <ir.json> -o <dir> [--emit-pgg scene.pgg]\n"
                 "       BuildingGeneratorCli check <ir.json>\n"
                 "       BuildingGeneratorCli --selftest\n");
    return 1;
}
