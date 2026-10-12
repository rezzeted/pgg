#define SDL_MAIN_HANDLED

#include "app_ui.hpp"
#include "map_preview.hpp"
#include "viewer_state.hpp"

#include "drui/drui.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#include <objbase.h>
#endif

namespace vv {

using namespace village_topology_generator;

namespace {

Roster make_roster() {
    Roster roster;
    roster.push_back({"mill", {SiteReq::near_water}, {}});
    roster.push_back({"bakery", {}, {"mill"}});
    roster.push_back({"tavern", {SiteReq::near_center}, {}});
    roster.push_back({"well", {SiteReq::near_center}, {}});
    for (int i = 0; i < g_house_count; ++i) {
        roster.push_back({"house", {}, {}});
    }
    return roster;
}

double elapsed_ms(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since)
        .count();
}

}  // namespace

void run_generation() {
    const auto t0 = std::chrono::steady_clock::now();
    app_log_push_fmt("Generate: seed=%llu map=%dx%d style=%s",
                     static_cast<unsigned long long>(g_terrain_params.seed), g_terrain_params.width,
                     g_terrain_params.height, g_style == 1 ? "trunk" : "radial");

    auto t = std::chrono::steady_clock::now();
    g_terrain = generate_terrain(g_terrain_params);
    app_log_push_fmt("  V1 terrain: %.1f ms", elapsed_ms(t));

    t = std::chrono::steady_clock::now();
    g_analysis = analyze_terrain(g_terrain);
    app_log_push_fmt("  V2 analysis: %.1f ms", elapsed_ms(t));

    t = std::chrono::steady_clock::now();
    g_center = pick_center(g_analysis);
    g_anchors.clear();
    g_anchors.push_back(
        {AnchorKind::center, g_center, g_analysis.attractiveness_at(g_center.first, g_center.second)});
    std::vector<std::pair<int, int>> road_targets;
    if (const auto ws = pick_water_site(g_terrain, g_analysis, g_center)) {
        g_anchors.push_back(
            {AnchorKind::water_site, *ws, g_analysis.attractiveness_at(ws->first, ws->second)});
        road_targets.push_back(*ws);
    }
    for (const auto& an : pick_edge_anchors(g_terrain, g_analysis)) {
        g_anchors.push_back(an);
        road_targets.push_back(an.cell);
    }
    app_log_push_fmt("  V3 anchors: %.1f ms (%zu)", elapsed_ms(t), g_anchors.size());

    t = std::chrono::steady_clock::now();
    g_skeleton_params.style = g_style == 1 ? GrowthStyle::trunk : GrowthStyle::radial;
    g_network = grow_skeleton(g_analysis, g_center, road_targets, g_skeleton_params);
    app_log_push_fmt("  V4 roads: %.1f ms (%zu roads, %zu/%zu anchors reached)", elapsed_ms(t),
                     g_network.roads.size(), g_network.reached.size(), road_targets.size());

    t = std::chrono::steady_clock::now();
    g_parcels = grow_parcels(g_analysis, g_network, g_parcel_params);
    app_log_push_fmt("  V5 parcels: %.1f ms (%zu)", elapsed_ms(t), g_parcels.size());

    t = std::chrono::steady_clock::now();
    const Roster roster = make_roster();
    g_placement = place_roster(g_terrain, g_analysis, g_parcels, roster, g_center, g_placement_params);
    for (const auto& v : validate_placement(roster, g_placement)) {
        app_log_push_fmt("  placement violation: %s", v.c_str());
    }
    app_log_push_fmt("  V6 placement: %.1f ms (%zu placed, %zu unplaced)", elapsed_ms(t),
                     roster.size() - g_placement.unplaced.size(), g_placement.unplaced.size());

    g_ir = build_layout_ir(g_terrain_params, g_network, g_parcels, g_placement, g_anchors);
    g_fingerprint = fingerprint_hex(layout_fingerprint(g_ir));
    g_last_ms = elapsed_ms(t0);
    g_generated = true;
    ++g_generation_version;
    app_log_push_fmt("Done: %.1f ms total, fingerprint %s", g_last_ms, g_fingerprint.c_str());
}

namespace {

int run_smoke() {
    g_terrain_params.seed = 11;
    g_house_count = 6;
    run_generation();
    std::printf("VillageTopologyGeneratorViewer smoke ok: fingerprint=%s roads=%zu parcels=%zu buildings=%zu\n",
                g_fingerprint.c_str(), g_network.roads.size(), g_parcels.size(),
                g_ir["buildings"].size());
    return 0;
}

void export_ir_json() {
    const std::string path = "village_layout_ir.json";
    const std::string text = g_ir.dump(2);
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
        app_log_push_fmt("Exported IR to %s (%zu buildings)", path.c_str(), g_ir["buildings"].size());
    } else {
        app_log_push_fmt("Export failed: cannot open %s", path.c_str());
    }
}

}  // namespace

}  // namespace vv

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--smoke") {
            return vv::run_smoke();
        }
    }

#if defined(_WIN32)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif

    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    constexpr float k_initial_window_scale = 1.5f;
    const int window_width = static_cast<int>(1600 * k_initial_window_scale);
    const int window_height = static_cast<int>(1000 * k_initial_window_scale);
    const SDL_WindowFlags window_flags =
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window =
        SDL_CreateWindow("Village Topology Generator Viewer", window_width, window_height, window_flags);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;
    io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports;

    int lw = 0, lh = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(window, &lw, &lh);
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    const float fb_scale = (lw > 0) ? static_cast<float>(pw) / static_cast<float>(lw) : 1.0f;
    float display_scale = SDL_GetWindowDisplayScale(window);
    if (display_scale < 0.01f) display_scale = 1.0f;
    const float content_scale = std::clamp(display_scale, 1.0f, 3.0f);
    const float dpi_scale = content_scale / std::max(fb_scale, 0.001f);
    ImGui::GetStyle().FontScaleMain = 1.0f / std::max(fb_scale, 0.001f);

    if (ImFont* default_font = DrUI::SetupFonts(io, content_scale)) {
        io.FontDefault = default_font;
    }
    DrUI::ApplyTheme(DrUI::ThemeId::Dark, dpi_scale);

    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
#if defined(__APPLE__)
    ImGui_ImplOpenGL3_Init("#version 150");
#else
    ImGui_ImplOpenGL3_Init("#version 130");
#endif

    vv::app_log_push("Set parameters and click Generate.");

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window)) {
                running = false;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        bool request_quit = false;
        bool export_ir = false;
        vv::draw_main_menu_bar(request_quit, export_ir);
        if (request_quit) running = false;
        if (export_ir && vv::g_generated) vv::export_ir_json();

        ImGuiViewport* vp = ImGui::GetMainViewport();
        const float gap = 8.0f;
        const float status_h = 34.0f;
        const float left_w = 320.0f;
        const float bottom_h = 180.0f;
        const ImVec2 wp = vp->WorkPos;
        const ImVec2 ws = vp->WorkSize;

        const ImVec2 left_pos(wp.x + gap, wp.y + gap);
        const ImVec2 left_size(left_w, ws.y - status_h - 2.0f * gap);
        const ImVec2 center_pos(wp.x + left_w + 2.0f * gap, wp.y + gap);
        const ImVec2 center_size(ws.x - left_w - 3.0f * gap, ws.y - bottom_h - status_h - 3.0f * gap);
        const ImVec2 bottom_pos(center_pos.x, center_pos.y + center_size.y + gap);
        const ImVec2 bottom_size(center_size.x, bottom_h);

        vv::draw_left_settings_panel(left_pos, left_size);
        vv::draw_map_preview(center_pos, center_size);
        vv::draw_bottom_log_panel(bottom_pos, bottom_size);
        vv::draw_status_bar(status_h);

        DrUI::ToastAnchor anchor{center_pos, center_size};
        DrUI::DrawToasts(anchor);

        ImGui::Render();
        SDL_GL_MakeCurrent(window, gl_context);
        const int fbw = static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x);
        const int fbh = static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y);
        glViewport(0, 0, fbw, fbh);
        glClearColor(DrUI::Colors::BackgroundPrimary.x, DrUI::Colors::BackgroundPrimary.y,
                     DrUI::Colors::BackgroundPrimary.z, DrUI::Colors::BackgroundPrimary.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

#if defined(_WIN32)
    CoUninitialize();
#endif
    return 0;
}
