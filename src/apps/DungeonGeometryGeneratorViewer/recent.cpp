#include "pch.h"

#include "recent.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

std::string configDir() {
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata)
        return std::string(appdata) + "\\DGGViewer";
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::string(home) + "/Library/Application Support/DGGViewer";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::string(xdg) + "/dgg-viewer";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::string(home) + "/.config/dgg-viewer";
#endif
    return {};
}

std::string trimLine(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}

}  // namespace

std::vector<std::string> loadRecentProjects() {
    std::vector<std::string> out;
    const std::string dir = configDir();
    if (dir.empty()) return out;
    std::ifstream in(dir + "/recent_projects.txt");
    std::string line;
    while (out.size() < 8 && std::getline(in, line)) {
        line = trimLine(line);
        if (!line.empty() && std::find(out.begin(), out.end(), line) == out.end())
            out.push_back(line);
    }
    return out;
}

void saveRecentProjects(const std::vector<std::string>& recent) {
    const std::string dir = configDir();
    if (dir.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream out(dir + "/recent_projects.txt", std::ios::binary | std::ios::trunc);
    if (!out) return;
    for (const std::string& p : recent) out << p << '\n';
}
