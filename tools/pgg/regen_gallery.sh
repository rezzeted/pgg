#!/usr/bin/env bash
# Capture the GitHub README hero shots into docs/gallery/.
# See docs/gallery/README.md for the camera list and why each angle.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/docs/gallery"
mkdir -p "$OUT"

viewer=""
if [[ -n "${PGG_VIEWER:-}" && -x "$PGG_VIEWER" ]]; then
    viewer="$PGG_VIEWER"
else
    for cand in \
        "$ROOT/_int_linux_release/src/apps/PggViewer/Release/PggViewer" \
        "$ROOT/_int_linux/src/apps/PggViewer/Debug/PggViewer" \
        "$ROOT/_intermediate_64/src/apps/PggViewer/Release/PggViewer" \
        "$ROOT/_intermediate_64/src/apps/PggViewer/Debug/PggViewer"
    do
        if [[ -x "$cand" ]]; then
            viewer="$cand"
            break
        fi
    done
fi
if [[ -z "$viewer" ]]; then
    echo "regen_gallery: PggViewer not found. Build PggViewer or set PGG_VIEWER." >&2
    exit 1
fi

run_viewer() {
    if command -v xvfb-run >/dev/null 2>&1; then
        xvfb-run -a -s "-screen 0 1440x900x24" "$@"
        return
    fi
    if [[ -n "${DISPLAY:-}" ]]; then
        "$@"
        return
    fi
    echo "regen_gallery: no DISPLAY and no xvfb-run" >&2
    exit 1
}

CROP="$ROOT/tools/pgg/crop_gallery.py"
if ! python3 -c "from PIL import Image" >/dev/null 2>&1; then
    echo "regen_gallery: need Python 3 + Pillow (python3 -c 'from PIL import Image')" >&2
    exit 1
fi

shot() {
    local file="$1" node="$2" orbit="$3" png="$4"
    echo "regen_gallery: $png  ($file  node=$node  orbit=$orbit)"
    run_viewer "$viewer" "$ROOT/$file" \
        --preview="$node" \
        --preview-orbit="$orbit" \
        --chrome=off \
        --shot-frame=preview \
        --shot="$OUT/$png"
    python3 "$CROP" "$OUT/$png"
}

# Hero cameras: 3/4 perspective, not ortho. Zoom ~1 so the AABB is inside the
# frame; crop_gallery.py then squares around the silhouette. Angles in
# docs/gallery/README.md.
shot resources/AmberEstate/spire_house.pgg house 38,18,1.02 spire_house.png
shot resources/AmberEstate/cottage.pgg     house 28,14,1.00 cottage.png
shot resources/pgg/inn_hotel.pgg              hotel 45,28,1.08 inn_hotel.png
shot resources/AmberEstate/stone_arch.pgg   scene 25,14,0.82 stone_arch.png

echo "regen_gallery: wrote $OUT"
ls -l "$OUT"/*.png
