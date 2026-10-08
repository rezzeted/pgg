#define SDL_MAIN_HANDLED

#include "app_state.hpp"
#include "app_ui.hpp"
#include "editor_layout.hpp"
#include "file_dialogs.hpp"
#include "layout_preview.hpp"
#include "preset_loader.hpp"

#include "drui/drui.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <objbase.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

#include "dungeon_topology_generator/dungeon_topology_generator.hpp"
#include "dungeon_topology_generator/generator/grid2d/layout_door_computation.hpp"
#include "dungeon_topology_generator/io/layout_json.hpp"

namespace ls {

dungeon_topology_generator::generator::grid2d::PresetCatalog g_catalog;
bool g_catalog_loaded = false;
int g_selected_preset = 0;

dungeon_topology_generator::generator::grid2d::GraphBasedGeneratorConfiguration g_gen_config;

std::vector<dungeon_topology_generator::generator::grid2d::LayoutGrid2D<int>> g_layouts;
int g_layout_index = 0;
bool g_use_random_seed = false;
int g_seed = 42;
int g_num_layouts = 1;
double g_last_time_ms = 0.0;
int g_last_iterations = 0;
int g_last_rooms = 0;

bool g_compute_doors = true;
bool g_export_pending = false;

int g_time_budget_ms = 15000;

char g_resources_path[1024] = "";
bool g_catalog_from_argv = false;

std::string get_executable_dir() {
    char path[1024] = {};
#ifdef _WIN32
    GetModuleFileNameA(nullptr, path, sizeof(path));
    char* last_slash = strrchr(path, '\\');
    if (last_slash) {
        *last_slash = '\0';
    }
#elif defined(__APPLE__)
    // macOS: resolve executable path via _NSGetExecutablePath
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        char* last_slash = strrchr(path, '/');
        if (last_slash) {
            *last_slash = '\0';
        }
    }
#else
    auto len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len > 0) {
        path[len] = '\0';
        char* last_slash = strrchr(path, '/');
        if (last_slash) {
            *last_slash = '\0';
        }
    }
#endif
    return std::string(path);
}

/// Walk up from the executable directory until `resources/dungeon_topology_generator_gui` contains Maps/ and Rooms/ (repo layout).
static std::string resolve_dungeon_topology_generator_gui_base_directory() {
    namespace fs = std::filesystem;
    try {
        fs::path dir = get_executable_dir();
        for (int depth = 0; depth < 12; ++depth) {
            const fs::path candidate = dir / "resources" / "dungeon_topology_generator_gui";
            if (fs::is_directory(candidate / "Maps") && fs::is_directory(candidate / "Rooms")) {
                std::error_code ec;
                const fs::path canon = fs::weakly_canonical(candidate, ec);
                return (ec ? candidate : canon).string();
            }
            if (!dir.has_parent_path()) {
                break;
            }
            const fs::path parent = dir.parent_path();
            if (parent == dir) {
                break;
            }
            dir = parent;
        }
    } catch (...) {
    }
    try {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path fb =
            fs::weakly_canonical(fs::path(get_executable_dir()) / "resources" / "dungeon_topology_generator_gui", ec);
        return (ec ? fs::path(get_executable_dir()) / "resources" / "dungeon_topology_generator_gui" : fb).string();
    } catch (...) {
        return get_executable_dir() + "/resources/dungeon_topology_generator_gui";
    }
}

static void assign_default_resources_path() {
    const std::string p = resolve_dungeon_topology_generator_gui_base_directory();
    std::strncpy(g_resources_path, p.c_str(), sizeof(g_resources_path) - 1);
    g_resources_path[sizeof(g_resources_path) - 1] = '\0';
}

void apply_catalog_load(dungeon_topology_generator::generator::grid2d::PresetCatalogLoadResult&& r) {
    using namespace dungeon_topology_generator::generator::grid2d;
    if (!r.error.empty()) {
        g_catalog = PresetCatalog{};
        g_catalog_loaded = false;
        g_selected_preset = 0;
        app_log_push_fmt("Load error: %s", r.error.c_str());
        return;
    }
    g_catalog = std::move(r.catalog);
    g_catalog_loaded = !g_catalog.maps.empty();
    g_selected_preset = 0;
    if (g_catalog_loaded) {
        app_log_push_fmt("Loaded %zu map(s), %zu room set(s) | base: %s", g_catalog.maps.size(),
                         g_catalog.room_sets.size(), g_catalog.base_path.c_str());
    } else {
        app_log_push_fmt("No maps found under: %s", g_catalog.base_path.c_str());
    }
}

void reload_catalog_from_resources_dir(const std::string& dir) {
    using namespace dungeon_topology_generator::generator::grid2d;
    apply_catalog_load(load_preset_catalog_with_status(dir));
}

void reload_catalog_from_map_file(const std::string& map_path) {
    using namespace dungeon_topology_generator::generator::grid2d;
    apply_catalog_load(load_preset_catalog_from_map_file(map_path));
}

void reload_catalog_from_default_resources() {
    assign_default_resources_path();
    reload_catalog_from_resources_dir(std::string(g_resources_path));
}

void parse_cli_args(int argc, char** argv) {
    assign_default_resources_path();

    std::string map_file_arg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.size() >= 4) {
            const bool yml = (a.size() >= 4 && a.compare(a.size() - 4, 4, ".yml") == 0);
            const bool yaml = (a.size() >= 5 && a.compare(a.size() - 5, 5, ".yaml") == 0);
            if (yml || yaml) {
                map_file_arg = a;
            }
        }
    }
    if (!map_file_arg.empty()) {
        reload_catalog_from_map_file(map_file_arg);
        g_catalog_from_argv = true;
        if (g_catalog_loaded && !g_catalog.base_path.empty()) {
            std::strncpy(g_resources_path, g_catalog.base_path.c_str(), sizeof(g_resources_path) - 1);
            g_resources_path[sizeof(g_resources_path) - 1] = '\0';
        }
    }
}

void generate_from_preset(int preset_idx, unsigned rng_seed) {
    using namespace dungeon_topology_generator::generator::grid2d;

    g_layouts.clear();
    g_layout_index = 0;

    const auto& map = g_catalog.maps[static_cast<std::size_t>(preset_idx)];
    LevelDescriptionGrid2D<int> level = build_level_from_preset(map, g_catalog);
    const int expected_rooms = static_cast<int>(level.get_graph().vertex_count());

    if (g_time_budget_ms > 0) {
        g_gen_config.early_stop_max_elapsed = std::chrono::milliseconds(g_time_budget_ms);
    } else {
        g_gen_config.early_stop_max_elapsed.reset();
    }
    GraphBasedGeneratorGrid2D<int> generator(level, g_gen_config);
    std::mt19937 rng(rng_seed);
    generator.inject_random_generator(std::move(rng));

    const int n = std::clamp(g_num_layouts, 1, 64);
    g_layouts.reserve(static_cast<size_t>(n));
    int incomplete_count = 0;
    for (int i = 0; i < n; ++i) {
        auto layout = generator.generate_layout();
        if (g_compute_doors) {
            std::mt19937 door_rng(rng_seed + static_cast<unsigned>(i));
            auto graph = level.get_graph();
            compute_layout_doors(layout, level, graph, door_rng);
        }
        g_layouts.push_back(std::move(layout));
        g_last_rooms = static_cast<int>(g_layouts.back().rooms.size());
        if (g_last_rooms < expected_rooms) {
            ++incomplete_count;
        }
        g_last_time_ms = generator.time_total_ms();
        g_last_iterations = generator.iterations_count();
    }

    int total_doors = 0;
    for (const auto& room : g_layouts[0].rooms) {
        total_doors += static_cast<int>(room.doors.size());
    }

    app_log_push_fmt("Generated %d layout(s) from '%s' | rooms=%d  doors=%d  time=%.2f ms  iters=%d", n,
                     map.display_name.c_str(), g_last_rooms, total_doors, g_last_time_ms, g_last_iterations);
    if (incomplete_count > 0) {
        app_log_push_fmt("Warning: %d of %d layout(s) incomplete (%d/%d rooms) - generation budget exhausted?",
                         incomplete_count, n, g_last_rooms, expected_rooms);
    }
}

void generate_hardcoded(unsigned rng_seed) {
    using namespace dungeon_topology_generator;
    using namespace dungeon_topology_generator::generator::grid2d;

    g_layouts.clear();
    g_layout_index = 0;

    auto square = RoomTemplateGrid2D(geometry::PolygonGrid2D::get_square(8),
                                     std::make_shared<SimpleDoorModeGrid2D>(1, 1));
    auto rectangle = RoomTemplateGrid2D(geometry::PolygonGrid2D::get_rectangle(6, 10),
                                        std::make_shared<SimpleDoorModeGrid2D>(1, 1));
    RoomDescriptionGrid2D room_desc(false, {square, rectangle});
    LevelDescriptionGrid2D<int> level;
    level.add_room(0, room_desc);
    level.add_room(1, room_desc);
    level.add_room(2, room_desc);
    level.add_room(3, room_desc);
    level.add_connection(0, 1);
    level.add_connection(0, 3);
    level.add_connection(1, 2);
    level.add_connection(2, 3);

    if (g_time_budget_ms > 0) {
        g_gen_config.early_stop_max_elapsed = std::chrono::milliseconds(g_time_budget_ms);
    } else {
        g_gen_config.early_stop_max_elapsed.reset();
    }
    GraphBasedGeneratorGrid2D<int> generator(level, g_gen_config);
    std::mt19937 rng(rng_seed);
    generator.inject_random_generator(std::move(rng));

    const int n = std::clamp(g_num_layouts, 1, 64);
    const int expected_rooms = 4;
    g_layouts.reserve(static_cast<size_t>(n));
    int incomplete_count = 0;
    for (int i = 0; i < n; ++i) {
        auto layout = generator.generate_layout();
        if (g_compute_doors) {
            std::mt19937 door_rng(rng_seed + static_cast<unsigned>(i));
            auto graph = level.get_graph();
            compute_layout_doors(layout, level, graph, door_rng);
        }
        g_layouts.push_back(std::move(layout));
        g_last_rooms = static_cast<int>(g_layouts.back().rooms.size());
        if (g_last_rooms < expected_rooms) {
            ++incomplete_count;
        }
        g_last_time_ms = generator.time_total_ms();
        g_last_iterations = generator.iterations_count();
    }

    app_log_push_fmt("Generated %d layout(s) (4-room cycle) | rooms=%d  time=%.2f ms  iters=%d", n, g_last_rooms,
                     g_last_time_ms, g_last_iterations);
    if (incomplete_count > 0) {
        app_log_push_fmt("Warning: %d of %d layout(s) incomplete (%d/%d rooms) - generation budget exhausted?",
                         incomplete_count, n, g_last_rooms, expected_rooms);
    }
}

} // namespace ls

int main(int argc, char* argv[])
{
#if defined(_WIN32)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif

    ls::parse_cli_args(argc, argv);

    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        (void)fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

#if defined(__APPLE__)
    // macOS only supports Core Profile starting at OpenGL 3.2
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
    const SDL_WindowFlags window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window = SDL_CreateWindow("LevelSynth", window_width, window_height, window_flags);
    if (!window) {
        (void)fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        (void)fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;
    io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports;

    int lw = 0, lh = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(window, &lw, &lh);
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    const float fb_scale = (lw > 0) ? static_cast<float>(pw) / static_cast<float>(lw) : 1.0f;

    float display_scale = SDL_GetWindowDisplayScale(window);
    if (display_scale < 0.01f) {
        display_scale = 1.0f;
    }
    const float content_scale = std::clamp(display_scale, 1.0f, 3.0f);
    const float dpi_scale = content_scale / std::max(fb_scale, 0.001f);
    const float font_scale = content_scale;
    ImGui::GetStyle().FontScaleMain = 1.0f / std::max(fb_scale, 0.001f);

    ImFont* default_font = DrUI::SetupFonts(io, font_scale);
    if (default_font) {
        io.FontDefault = default_font;
    }
    DrUI::ApplyTheme(DrUI::ThemeId::Dark, dpi_scale);

    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
#if defined(__APPLE__)
    // macOS Core Profile 3.2 requires GLSL 150
    ImGui_ImplOpenGL3_Init("#version 150");
#else
    ImGui_ImplOpenGL3_Init("#version 130");
#endif

    app_log_push("Select a preset and click Generate.");

    if (!ls::g_catalog_from_argv) {
        try {
            ls::reload_catalog_from_resources_dir(ls::g_resources_path);
        } catch (...) {
            ls::g_catalog_loaded = false;
            app_log_push("Exception while loading resources.");
        }
    }

    EditorSplitters splitters;
    PanelVisibility panels;
    panels.dpi_scale = dpi_scale;

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
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr) {
                std::string dropped(event.drop.data);
                SDL_free(const_cast<char*>(event.drop.data));
                try {
                    namespace fs = std::filesystem;
                    fs::path dp(dropped);
                    if (dp.has_extension()) {
                        const auto ext = dp.extension().string();
                        if (ext == ".yml" || ext == ".yaml") {
                            ls::reload_catalog_from_map_file(dropped);
                            if (ls::g_catalog_loaded && !ls::g_catalog.base_path.empty()) {
                                std::strncpy(ls::g_resources_path, ls::g_catalog.base_path.c_str(),
                                             sizeof(ls::g_resources_path) - 1);
                                ls::g_resources_path[sizeof(ls::g_resources_path) - 1] = '\0';
                            }
                        } else if (fs::is_directory(dp)) {
                            const fs::path maps = dp / "Maps";
                            const fs::path rooms = dp / "Rooms";
                            if (fs::is_directory(maps) && fs::is_directory(rooms)) {
                                std::strncpy(ls::g_resources_path, dropped.c_str(), sizeof(ls::g_resources_path) - 1);
                                ls::g_resources_path[sizeof(ls::g_resources_path) - 1] = '\0';
                                ls::reload_catalog_from_resources_dir(dropped);
                            } else {
                                app_log_push("Drop ignored: folder must contain Maps/ and Rooms/ (dungeon_topology_generator_gui root).");
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    app_log_push_fmt("Drop error: %s", e.what());
                }
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        float cs = std::max(io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        if (cs < 0.01f) {
            cs = 1.0f;
        }
        panels.dpi_scale = cs / std::max(fb_scale, 0.001f);
        const float gap = 8.0f * panels.dpi_scale;

        bool request_quit = false;
        bool menu_export = false;
        ls::draw_main_menu_bar(request_quit, menu_export, panels);
        if (request_quit) {
            running = false;
        }
        if (menu_export && !ls::g_layouts.empty()) {
            ls::g_export_pending = true;
        }

        bool shortcut_export = false;
        ls::handle_keyboard_shortcuts(shortcut_export);
        if (shortcut_export && !ls::g_layouts.empty()) {
            ls::g_export_pending = true;
        }

        ImGuiViewport* vp = ImGui::GetMainViewport();
        const float status_h = panels.status_bar ? ls::compute_status_bar_height(panels.dpi_scale) : 0.0f;
        const EditorLayout layout = CalculateLayout(vp, gap, 0.0f, status_h, splitters, panels);
        HandleSplitters(layout, gap, vp, 0.0f, status_h, splitters, panels);

        if (panels.show_left_panel) {
            ls::draw_left_settings_panel(layout.left);
        }
        ls::draw_center_preview_panel(layout.center);
        if (panels.show_log_panel) {
            ls::draw_bottom_log_panel(layout.bottom, panels);
        }

        DrawSplitterIndicators(layout, splitters, panels);

        DrUI::ToastAnchor anchor{layout.center.pos, layout.center.size};
        DrUI::DrawToasts(anchor);

        if (panels.status_bar) {
            ls::draw_status_bar(panels.dpi_scale, status_h);
        }

        if (ls::g_export_pending && !ls::g_layouts.empty()) {
            ls::g_export_pending = false;
            try {
                const auto& layout_json = ls::g_layouts[static_cast<size_t>(ls::g_layout_index)];
                auto j = dungeon_topology_generator::io::layout_to_json(layout_json);
                std::string json_str = j.dump(2);
                std::string out_path;
                if (pick_save_json_file(out_path)) {
                    auto f = fopen(out_path.c_str(), "wb");
                    if (f) {
                        fwrite(json_str.data(), 1, json_str.size(), f);
                        fclose(f);
                        namespace fs = std::filesystem;
                        std::error_code ec;
                        const fs::path abs = fs::weakly_canonical(fs::absolute(out_path), ec);
                        const std::string show = ec ? out_path : abs.string();
                        app_log_push_fmt("Exported layout to %s (%d rooms)", show.c_str(),
                                         static_cast<int>(layout_json.rooms.size()));
                    } else {
                        app_log_push_fmt("Export failed: cannot open %s", out_path.c_str());
                    }
                } else {
#if defined(_WIN32)
                    app_log_push("Export cancelled");
#else
                    std::string filename = "layout_export.json";
                    auto f = fopen(filename.c_str(), "wb");
                    if (f) {
                        fwrite(json_str.data(), 1, json_str.size(), f);
                        fclose(f);
                        namespace fs = std::filesystem;
                        std::error_code ec;
                        const fs::path abs = fs::weakly_canonical(fs::absolute(filename), ec);
                        const std::string show = ec ? filename : abs.string();
                        app_log_push_fmt("Exported layout to %s (no save dialog; %d rooms)", show.c_str(),
                                         static_cast<int>(layout_json.rooms.size()));
                    }
#endif
                }
            } catch (const std::exception& e) {
                app_log_push_fmt("Export error: %s", e.what());
            }
        }

        ImGui::Render();
        SDL_GL_MakeCurrent(window, gl_context);
        const int fb_w = static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x);
        const int fb_h = static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y);
        glViewport(0, 0, fb_w, fb_h);
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
