#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Application log (bottom panel). Thread: main/UI only.
void app_log_clear();
void app_log_push(const std::string& line);
void app_log_push_fmt(const char* fmt, ...);

extern std::vector<std::string> g_app_log;
inline constexpr std::size_t k_app_log_max_lines = 2000;
