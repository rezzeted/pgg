#include "pch.h"

#include "plan_svg.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace building_topology_generator {

namespace {

constexpr double kScale = 40.0;  // px per meter

struct Proj {
    double x0 = 0.0, z0 = 0.0;  // world min corner
    double height = 0.0;        // world depth
    double px(double x) const { return (x - x0) * kScale + kScale; }
    // Screen y down, facade (+Z) at the bottom: z_max maps to the bottom edge.
    double py(double z) const { return (z - z0) * kScale + kScale; }
};

std::string poly_points(const std::vector<glm::dvec2>& p, const Proj& pr) {
    std::ostringstream s;
    for (const glm::dvec2& v : p) s << pr.px(v.x) << "," << pr.py(v.y) << " ";
    return s.str();
}

void edge_line(std::ostringstream& svg, const glm::dvec3& a, const glm::dvec3& b, const Proj& pr,
               const char* color, double width) {
    svg << "<line x1=\"" << pr.px(a.x) << "\" y1=\"" << pr.py(a.z) << "\" x2=\"" << pr.px(b.x)
        << "\" y2=\"" << pr.py(b.z) << "\" stroke=\"" << color << "\" stroke-width=\"" << width
        << "\"/>\n";
}

}  // namespace

std::string storey_plan_svg(const Ir& ir, int storey_index, const Site& site) {
    // Sheet bounds: the site or the volume union, plus a 1 m margin.
    double x0 = -site.w / 2, x1 = site.w / 2, z0 = -site.d / 2, z1 = site.d / 2;
    if (site.w <= 0.0) {
        x0 = z0 = 1e30;
        x1 = z1 = -1e30;
        for (const IrVolume& v : ir.volumes)
            for (const glm::dvec2& p : v.outline) {
                x0 = std::min(x0, p.x);
                x1 = std::max(x1, p.x);
                z0 = std::min(z0, p.y);
                z1 = std::max(z1, p.y);
            }
    }
    Proj pr;
    pr.x0 = x0 - 1.0;
    pr.z0 = z0 - 1.0;
    const double sheet_w = (x1 - x0 + 2.0) * kScale + 2 * kScale;
    const double sheet_h = (z1 - z0 + 2.0) * kScale + 2 * kScale;

    std::ostringstream svg;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << sheet_w << "\" height=\""
        << sheet_h << "\" font-family=\"monospace\" font-size=\"12\">\n";
    svg << "<rect width=\"100%\" height=\"100%\" fill=\"#f8f7f4\"/>\n";

    const std::string storey_id =
        storey_index >= 0 && storey_index < static_cast<int>(ir.storeys.size())
            ? ir.storeys[storey_index].id
            : "?";

    // Site bounds.
    if (site.w > 0.0) {
        svg << "<rect x=\"" << pr.px(-site.w / 2) << "\" y=\"" << pr.py(-site.d / 2)
            << "\" width=\"" << site.w * kScale << "\" height=\"" << site.d * kScale
            << "\" fill=\"none\" stroke=\"#999\" stroke-dasharray=\"6,4\"/>\n";
    }

    // Volume footprints (only volumes spanning this storey).
    for (const IrVolume& v : ir.volumes) {
        if (storey_index < v.storey0 || storey_index > v.storey1) continue;
        const bool attic = storey_index < static_cast<int>(ir.storeys.size()) &&
                           ir.storeys[storey_index].kind_code == kStoreyAttic;
        svg << "<polygon points=\"" << poly_points(v.outline, pr) << "\" fill=\""
            << (attic ? "#e8dcc8" : "#d8dce4") << "\" stroke=\"#333\" stroke-width=\"2\"/>\n";
        // Label at the outline centroid.
        glm::dvec2 c{0.0};
        for (const glm::dvec2& p : v.outline) c += p;
        c /= static_cast<double>(v.outline.size());
        svg << "<text x=\"" << pr.px(c.x) << "\" y=\"" << pr.py(c.y)
            << "\" text-anchor=\"middle\" fill=\"#333\">" << v.id
            << (attic ? " (attic)" : "") << "</text>\n";
    }

    // Roof edge projection on the topmost plan (§15: ridge/valley projection).
    const bool topmost = storey_index == static_cast<int>(ir.storeys.size()) - 1;
    if (topmost) {
        for (const IrRoofEdge& e : ir.roof.ridges) edge_line(svg, e.a, e.b, pr, "#c0392b", 2.5);
        for (const IrRoofEdge& e : ir.roof.valleys) edge_line(svg, e.a, e.b, pr, "#2471a3", 2.0);
        for (const IrRoofEdge& e : ir.roof.hips) edge_line(svg, e.a, e.b, pr, "#1e8449", 2.0);
    }

    // Facade marker: +Z at the bottom.
    svg << "<text x=\"" << pr.px(0.0) << "\" y=\"" << pr.py(z1 + 0.6)
        << "\" text-anchor=\"middle\" fill=\"#666\">facade +Z</text>\n";
    svg << "<text x=\"" << kScale << "\" y=\"" << kScale * 0.7 << "\" fill=\"#333\">storey: "
        << storey_id << "</text>\n";
    svg << "</svg>\n";
    return svg.str();
}

}  // namespace building_topology_generator
