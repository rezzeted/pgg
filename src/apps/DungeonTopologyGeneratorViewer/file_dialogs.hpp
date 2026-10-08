#pragma once

#include <string>

#if defined(_WIN32)
bool pick_folder_dialog(std::string& out_path);
bool pick_open_yaml_file(std::string& out_path);
bool pick_save_json_file(std::string& out_path);
#else
// Non-Windows: use manual path entry in UI; dialogs return false.
inline bool pick_folder_dialog(std::string&) {
    return false;
}
inline bool pick_open_yaml_file(std::string&) {
    return false;
}
inline bool pick_save_json_file(std::string&) {
    return false;
}
#endif
