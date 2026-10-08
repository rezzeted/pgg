#include "app_state.hpp"

#include <cstdarg>
#include <cstdio>

std::vector<std::string> g_app_log;

void app_log_clear() {
    g_app_log.clear();
}

void app_log_push(const std::string& line) {
    g_app_log.push_back(line);
    while (g_app_log.size() > k_app_log_max_lines) {
        g_app_log.erase(g_app_log.begin());
    }
}

void app_log_push_fmt(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    app_log_push(std::string(buf));
}
