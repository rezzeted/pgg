#include "pch.h"

#include "project_tree.h"

#include <cstdio>

#include <imgui.h>

#include "level.h"

namespace {

std::string joinNames(const std::vector<std::string>& names) {
    std::string s;
    for (const std::string& n : names) {
        if (!s.empty()) s += ", ";
        s += n;
    }
    return s;
}

void roomTooltip(const dungeon_geometry_generator::GraphRoom& room) {
    ImGui::BeginTooltip();
    ImGui::Text("%s [%s]", room.id.c_str(), room.role.c_str());
    if (!room.tags.empty()) ImGui::Text("tags: %s", joinNames(room.tags).c_str());
    if (!room.fill.empty()) ImGui::TextDisabled("has fill overrides");
    ImGui::EndTooltip();
}

}  // namespace

ProjectTreeActions drawProjectTree(const Level& level) {
    ProjectTreeActions actions;
    if (!level.loaded) return actions;

    const dungeon_geometry_generator::Project& project = level.project;
    ImGui::Text("seed %d", project.seed);
    if (!project.layout) {
        ImGui::TextDisabled("project/0: no layout tier");
        return actions;
    }
    const dungeon_geometry_generator::LayoutParams& layout = *project.layout;
    const dungeon_geometry_generator::layout::Catalog& catalog = level.catalog;

    ImGui::TextDisabled("catalog: %d templates, %d instances (budget %d)", catalog.stats.templates,
                        catalog.stats.instances, layout.catalog_budget);

    if (ImGui::TreeNode("rooms", "Rooms (%zu)", layout.rooms.size())) {
        for (const dungeon_geometry_generator::GraphRoom& room : layout.rooms) {
            char label[256];
            std::snprintf(label, sizeof(label), "%s  [%s]##room", room.id.c_str(), room.role.c_str());
            if (ImGui::Selectable(label)) {
                actions.selectRoom = true;
                actions.roomId = room.id;
            }
            if (ImGui::IsItemHovered()) roomTooltip(room);
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("passages", "Passages (%zu)", layout.passages.size())) {
        for (const dungeon_geometry_generator::Passage& p : layout.passages)
            ImGui::BulletText("%s - %s (%s)", p.a.c_str(), p.b.c_str(), p.door.c_str());
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("templates", "Templates (%d)", catalog.stats.templates)) {
        ImGui::TextDisabled("%d parametric (%d corridors + %d rects), %d explicit",
                            catalog.stats.corridors + catalog.stats.rects, catalog.stats.corridors,
                            catalog.stats.rects, catalog.stats.explicit_count);
        if (ImGui::TreeNode("generators", "Generators")) {
            char label[96];
            std::snprintf(label, sizeof(label), "rooms_rect (%d)##gen", catalog.stats.rects);
            if (ImGui::Selectable(label)) {
                actions.openTab = true;
                actions.tab = {ProjectTreeSelection::Kind::GeneratorRects, {}};
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("rect %d..%d x %d..%d cells", layout.rect_w.lo, layout.rect_w.hi,
                                  layout.rect_h.lo, layout.rect_h.hi);
            std::snprintf(label, sizeof(label), "corridors (%d)##gen", catalog.stats.corridors);
            if (ImGui::Selectable(label)) {
                actions.openTab = true;
                actions.tab = {ProjectTreeSelection::Kind::GeneratorCorridors, {}};
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("width %d, length %d..%d cells", layout.corridor_width,
                                  layout.corridor_length.lo, layout.corridor_length.hi);
            ImGui::TreePop();
        }
        for (const dungeon_geometry_generator::layout::CatalogEntry& e : catalog.entries) {
            if (e.parametric) continue;
            char label[256];
            std::snprintf(label, sizeof(label), "%s##tmpl", e.name.c_str());
            if (ImGui::Selectable(label)) {
                actions.openTab = true;
                actions.tab = {ProjectTreeSelection::Kind::Template, e.name};
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("explicit template, roles: %s", joinNames(e.roles).c_str());
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("fill", "Fill")) {
        for (const auto& [name, role] : project.fill.roles) {
            ImGui::BulletText("%s: h=%.1f style=%s floor=%s ceil=%s", name.c_str(), role.h,
                              role.style.c_str(), role.floor.c_str(), role.ceil.c_str());
        }
        ImGui::BulletText("transitions: %s, width %.2f, %s", project.fill.transitions.pattern.c_str(),
                          project.fill.transitions.width, project.fill.transitions.place.c_str());
        ImGui::BulletText("side rules: %zu, decor rules: %zu", project.fill.side_rules.size(),
                          project.fill.decor.size());
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("slots", "Slots (%zu)", project.slots.size())) {
        for (const auto& [slot, path] : project.slots)
            ImGui::BulletText("%s -> %s", slot.c_str(), path.c_str());
        ImGui::TreePop();
    }

    return actions;
}
