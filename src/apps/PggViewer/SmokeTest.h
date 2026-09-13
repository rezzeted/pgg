#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Screenshot pixel crop (F1, implemented in main.cpp): copies the rect
// (x, y, w, h) — top-down framebuffer pixels — out of an RGBA8 buffer,
// clamped to the buffer bounds; false when the clamped rect is empty.
bool cropShotPixels(const std::vector<std::uint8_t>& src, int srcW, int srcH, int x, int y, int w, int h,
                    std::vector<std::uint8_t>& out, int& outW, int& outH);

// CPU smoke test of the viewer (--smoke), run before any sokol init: graph
// derivation on the pgg corpus, layout determinism, camera math
// (setTarget/ortho, setZoom/setDistance), the F1 screenshot crop on a
// synthetic buffer and the F3 CPU helpers (FrameCompare) on synthetic buffers.
// Logs TEST PASS / TEST FAIL lines; returns true when everything passed.
bool runPggViewerSmokeTest();
