#pragma once

#include <imgui.h>

namespace DrUI {

// Load Cyrillic-capable system font + merge Font Awesome icon font.
// Returns the default font (base size) or nullptr on failure.
ImFont* SetupFonts(ImGuiIO& io, float dpi_scale);

} // namespace DrUI
