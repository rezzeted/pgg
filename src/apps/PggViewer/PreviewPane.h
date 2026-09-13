#pragma once

#include "GeometryPreview.h"

#include <imgui.h>

struct PreviewPaneRect {
    int x = 0, y = 0, w = 0, h = 0;
};

// ImGui body of the preview pane (toolbar, image, orbit/pan/zoom).
void drawPreviewWindowContents(GeometryPreview& preview, PreviewPaneRect& lastImageRectPx);
