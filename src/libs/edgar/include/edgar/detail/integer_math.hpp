#pragma once

namespace edgar::detail {

inline int mod_360(int degrees) {
    int r = degrees % 360;
    if (r < 0) {
        r += 360;
    }
    return r;
}

inline int int_sin_deg(int degrees) {
    switch (mod_360(degrees)) {
        case 0:
            return 0;
        case 90:
            return 1;
        case 180:
            return 0;
        case 270:
            return -1;
        default:
            return 0;
    }
}

inline int int_cos_deg(int degrees) {
    switch (mod_360(degrees)) {
        case 0:
            return 1;
        case 90:
            return 0;
        case 180:
            return -1;
        case 270:
            return 0;
        default:
            return 0;
    }
}

} // namespace edgar::detail
