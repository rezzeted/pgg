#pragma once

// Readback of GeometryPreview's 1-sample color target to a top-down RGBA8
// buffer. Call after sg_commit() of the frame that ran GeometryPreview::render().
// Sokol has no portable readback API; each backend uses native texture query
// (sg_gl/mtl/d3d11_query_image_info).

#include "GeometryPreview.h"

#include <cstdint>
#include <string>
#include <vector>

struct PreviewCaptureResult {
    bool ok = false;
    int width = 0;
    int height = 0;
    bool sizeClamped = false;
    std::vector<std::uint8_t> pixels;  // top-down RGBA8, empty when !ok
};

PreviewCaptureResult capturePreview(const GeometryPreview& preview);

// stb_image_write PNG. false on I/O failure.
bool writePngRgba(const char* path, int width, int height, const std::vector<std::uint8_t>& pixels);
