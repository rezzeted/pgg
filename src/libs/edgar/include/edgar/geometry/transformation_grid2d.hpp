#pragma once

namespace edgar::geometry {

enum class TransformationGrid2D {
    Identity = 0,
    Rotate90,
    Rotate180,
    Rotate270,
    MirrorX,
    MirrorY,
    Diagonal13,
    Diagonal24,
};

} // namespace edgar::geometry
