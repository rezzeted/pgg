#pragma once

// Windowless GPU context for PggServe --headless. Linux: GL 3.3 core on a
// 64x64 GLX pbuffer — the same context the tiny window provided, but nothing
// is ever mapped; the pbuffer exists only to make the context current, all
// rendering targets the preview FBOs. Other platforms: setup fails with a
// clear message and PggServe keeps its windowed path.
// Own TU on purpose: sokol_app.h redeclares the GLX types, so <GL/glx.h>
// cannot be included alongside it.

#include <string>

struct PggHeadlessGl {
    void* display = nullptr;    // Display*
    unsigned long pbuffer = 0;  // GLXPbuffer
    void* context = nullptr;    // GLXContext
};

bool pggHeadlessGlSetup(PggHeadlessGl& gl, std::string& err);
void pggHeadlessGlTeardown(PggHeadlessGl& gl);
