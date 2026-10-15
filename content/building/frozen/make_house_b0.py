#!/usr/bin/env python3
"""Frozen B0 house: hand-authored plan mechanically expanded into IR v0
(building-topology-generator-ir/0, requirements §10, §16 B0).

The house: 8 x 7 m on wall axes, two full storeys + attic under a gable roof
(50 deg slopes on the z sides, gables at x = +-4), one L-stair shaft stacked
through both floors (stair2 is plan-identical to stair1, which keeps flight
headroom constant), central chimney (2 x 1 cells) with two back-to-back
fireplaces on the ground floor.

Coordinates are meters in the building frame (§5.1): plan in XZ, facade +Z,
origin at the footprint center. The plan is authored on the 0.5 m cell grid;
walls, nodes, slabs, wells, railings and outlines are derived mechanically so
the JSON stays consistent by construction.

Usage: python3 make_house_b0.py [out.json]   (default: house_b0.ir.json here)
"""

import json
import math
import sys
from collections import defaultdict

# ---------------------------------------------------------------- params

CELL = 0.5
X0, X1 = -4.0, 4.0      # wall axis rectangle
Z0, Z1 = -3.5, 3.5
NX, NZ = 16, 14         # cells

T_EXTERIOR = 0.45
T_BEARING = 0.30
T_PARTITION = 0.12
T_KNEE = 0.10

H_GROUND = 2.8
H_UPPER = 2.6
SLAB_T = 0.25
Y_UPPER = H_GROUND + SLAB_T          # 3.05
Y_ATTIC = Y_UPPER + H_UPPER + SLAB_T  # 5.90
H_EAVE = 0.8                          # attic exterior wall height to the eave
EAVE_Y = Y_ATTIC + H_EAVE             # 6.70 over the outline

PITCH = 50.0                          # degrees, edges 1 and 3 (z sides)
OVERHANG = 0.4
ROOF_T = 0.25

TAN = math.tan(math.radians(PITCH))   # 1.1917536
COS = math.cos(math.radians(PITCH))
SIN = math.sin(math.radians(PITCH))

HALF_OUT = T_EXTERIOR / 2  # 0.225: outline offset from the axis rectangle
OUT_X = abs(X0) + HALF_OUT  # 4.225
OUT_Z = abs(Z0) + HALF_OUT  # 3.725

Y0 = EAVE_Y - OVERHANG * TAN          # slope surface over the swept (offset) outline edge
RIDGE_HALF = OUT_X + OVERHANG         # ridge runs to the offset gable edges
RIDGE_Y = Y0 + (OUT_Z + OVERHANG) * TAN

# Roof planes (as build_roof_model derives them): one per sloped edge.
PLANE_F = {"o": (0.0, Y0, OUT_Z + OVERHANG), "n": (0.0, COS, SIN)}    # facade slope
PLANE_B = {"o": (0.0, Y0, -OUT_Z - OVERHANG), "n": (0.0, COS, -SIN)}  # back slope

# Ceiling (underside) planes: shifted along -n by roof_t (perpendicular).
def ceiling_plane(pl):
    o = tuple(pl["o"][i] - pl["n"][i] * ROOF_T for i in range(3))
    return {"o": o, "n": pl["n"]}

CEIL_F = ceiling_plane(PLANE_F)
CEIL_B = ceiling_plane(PLANE_B)

# Useful attic zone: ceiling height >= 2.0 above the attic floor (§5.6).
Z_USEFUL = OUT_Z - (2.0 + ROOF_T / COS + Y_ATTIC - EAVE_Y) / TAN  # ~2.3917

# Chimney
CHIM_X0, CHIM_X1 = -0.5, 0.5
CHIM_Z0, CHIM_Z1 = 0.0, 0.5
CHIM_TOP_Y = round(RIDGE_Y, 4) + 0.5  # d = 0 to the ridge -> ridge + 0.5 (§5.7)

# Stair shaft (plan-identical on both floors)
ST_Z0, ST_Z1 = -3.275, -2.275   # march band, flush to the back wall face
ST_M1_X0 = -3.0                 # march 1 base (first riser x)
ST_LAND_X0, ST_LAND_X1 = -1.0, 0.0
ST_M2_X0, ST_M2_X1 = -1.0, 0.0  # march 2 band
ST_M2_Z0, ST_M2_Z1 = -2.275, -0.775
WELL_A_1 = (-2.0, ST_Z0, 0.0, ST_Z1)      # slab1 opening (stair1)
WELL_B_1 = (-1.0, ST_M2_Z0, 0.0, ST_M2_Z1)
WELL_A_2 = (-2.5, ST_Z0, 0.0, ST_Z1)      # slab2 opening (stair2, finer rise)
WELL_B_2 = (-1.0, ST_M2_Z0, 0.0, ST_M2_Z1)

# ---------------------------------------------------------------- helpers

def q(v):
    r = round(float(v), 4)
    return 0.0 if r == 0 else r

def v2(x, z):
    return [q(x), q(z)]

def v3(x, y, z):
    return [q(x), q(y), q(z)]

def cell_of(x, z):
    return (int(math.floor((x - X0) / CELL + 1e-9)), int(math.floor((z - Z0) / CELL + 1e-9)))

def cell_center(i, j):
    return (X0 + (i + 0.5) * CELL, Z0 + (j + 0.5) * CELL)

def plane_json(pl):
    return {"o": v3(*pl["o"]), "n": v3(*pl["n"])}

# ---------------------------------------------------------------- label maps
# labels[i][j], i along x, j along z. Extra labels: "well" (stair opening),
# "void" (attic roof void), None outside the grid.

def make_labels(storey):
    lab = [[None] * NZ for _ in range(NX)]
    if storey == "ground":
        for i in range(NX):
            for j in range(NZ):
                if j <= 5:
                    lab[i][j] = "stairwell" if i <= 7 else ("corridor_g" if i <= 11 else "pantry")
                else:
                    lab[i][j] = "hall" if i <= 7 else "parlour"
    elif storey == "upper":
        for i in range(NX):
            for j in range(NZ):
                if j <= 5:
                    lab[i][j] = "corridor_u" if (i >= 3 or j <= 1) else "storage"
                else:
                    lab[i][j] = "bed1" if i <= 7 else "bed2"
        for (x0, z0, x1, z1) in (WELL_A_1, WELL_B_1):
            for i in range(NX):
                for j in range(NZ):
                    cx, cz = cell_center(i, j)
                    if x0 - 1e-9 <= cx <= x1 + 1e-9 and z0 - 1e-9 <= cz <= z1 + 1e-9:
                        lab[i][j] = "well"
    else:  # attic
        for i in range(NX):
            for j in range(NZ):
                if j == 0 or j == NZ - 1:
                    lab[i][j] = "void"
                elif i <= 7:
                    lab[i][j] = "storage2" if j <= 4 else "corridor_a"
                else:
                    lab[i][j] = "bed3"
        for (x0, z0, x1, z1) in (WELL_A_2, WELL_B_2):
            for i in range(NX):
                for j in range(NZ):
                    cx, cz = cell_center(i, j)
                    if x0 - 1e-9 <= cx <= x1 + 1e-9 and z0 - 1e-9 <= cz <= z1 + 1e-9:
                        lab[i][j] = "well" if lab[i][j] != "void" else "void"
    return lab

LABELS = {s: make_labels(s) for s in ("ground", "upper", "attic")}

SPACE_META = {
    # id: (storey, fn, fn_code, zone, zone_code, habitable, hearth_code)
    "hall":       ("ground", "hall", 2, "public", 1, True, 1),
    "parlour":    ("ground", "living", 4, "public", 1, True, 1),
    "stairwell":  ("ground", "corridor", 11, "service", 2, False, 0),
    "corridor_g": ("ground", "corridor", 11, "service", 2, False, 0),
    "pantry":     ("ground", "storage", 6, "service", 2, False, 0),
    "bed1":       ("upper", "bedroom", 5, "private", 3, True, 0),
    "bed2":       ("upper", "bedroom", 5, "private", 3, True, 0),
    "corridor_u": ("upper", "corridor", 11, "service", 2, False, 0),
    "storage":    ("upper", "storage", 6, "service", 2, False, 0),
    "bed3":       ("attic", "bedroom", 5, "private", 3, True, 0),
    "corridor_a": ("attic", "corridor", 11, "service", 2, False, 0),
    "storage2":   ("attic", "storage", 6, "service", 2, False, 0),
}

STOREY_Y = {"ground": 0.0, "upper": Y_UPPER, "attic": Y_ATTIC}
STOREY_H = {"ground": H_GROUND, "upper": H_UPPER, "attic": H_EAVE}

# ---------------------------------------------------------------- walls

def wall_class_for(storey, vertical, coord, la, lb):
    """(class_name, class_code, thickness) for a boundary on the given grid line."""
    pair = {la, lb}
    if "outside" in pair:
        if storey == "attic":
            if vertical:  # x = +-4: gable ends
                return ("gable", 4, T_EXTERIOR)
            return ("exterior", 1, T_EXTERIOR)
        return ("exterior", 1, T_EXTERIOR)
    if "void" in pair:
        return ("knee", 5, T_KNEE)
    if storey in ("ground", "upper"):
        if (vertical and abs(coord) < 1e-9) or (not vertical and abs(coord + 0.5) < 1e-9):
            return ("bearing", 2, T_BEARING)
    return ("partition", 3, T_PARTITION)

def build_walls(storey):
    """Boundary tracing on the label map; collinear runs with identical
    (class, sides) merge into one wall. Returns (walls, railings)."""
    lab = LABELS[storey]
    segs = []  # (vertical, coord, lo, hi, la, lb) with la on the -x/-z side

    def label_at(i, j):
        if 0 <= i < NX and 0 <= j < NZ:
            return lab[i][j]
        return "outside"

    # vertical boundaries (x = X0 + i*CELL) between cell (i-1, j) and (i, j)
    for gi in range(NX + 1):
        x = X0 + gi * CELL
        j = 0
        while j < NZ:
            a, b = label_at(gi - 1, j), label_at(gi, j)
            # a well on the outer boundary still needs the exterior wall behind it
            pair = {a, b}
            skip = a == b or ("well" in pair and "outside" not in pair)
            if skip:
                j += 1
                continue
            j0 = j
            while j + 1 < NZ:
                a2, b2 = label_at(gi - 1, j + 1), label_at(gi, j + 1)
                if (a2, b2) != (a, b):
                    break
                j += 1
            segs.append((True, x, Z0 + j0 * CELL, Z0 + (j + 1) * CELL, a, b))
            j += 1
    # horizontal boundaries
    for gj in range(NZ + 1):
        z = Z0 + gj * CELL
        i = 0
        while i < NX:
            a, b = label_at(i, gj - 1), label_at(i, gj)
            pair = {a, b}
            skip = a == b or ("well" in pair and "outside" not in pair)
            if skip:
                i += 1
                continue
            i0 = i
            while i + 1 < NX:
                a2, b2 = label_at(i + 1, gj - 1), label_at(i + 1, gj)
                if (a2, b2) != (a, b):
                    break
                i += 1
            segs.append((False, z, X0 + i0 * CELL, X0 + (i + 1) * CELL, a, b))
            i += 1

    walls = []
    railings = []
    for (vertical, coord, lo, hi, la, lb) in segs:
        if "void" in (la, lb) and storey != "attic":
            continue
        cls = wall_class_for(storey, vertical, coord, la, lb)
        if vertical:
            # wall a->b goes +z; side_l is -x side (la), side_r is +x side (lb)
            a, b = (coord, lo), (coord, hi)
            side_l, side_r = la, lb
        else:
            a, b = (lo, coord), (hi, coord)
            side_l, side_r = lb, la  # side_l is +z side
        if cls[0] == "knee" and storey == "attic":
            pass  # knee walls between room and roof void
        walls.append({
            "vertical": vertical, "coord": coord, "lo": lo, "hi": hi,
            "a": a, "b": b, "side_l": side_l, "side_r": side_r,
            "class": cls[0], "class_code": cls[1], "thickness": cls[2],
        })
    return walls

# Railings: exact edges of the stair well rects (cell maps are too coarse —
# a railing must stand exactly on the slab edge). Edges that coincide with an
# exterior wall inner face (the back wall at z = Z0 + 0.225) need no railing.
def build_railings(storey):
    wells = {"upper": [WELL_A_1, WELL_B_1], "attic": [WELL_A_2, WELL_B_2]}.get(storey, [])
    face_z = Z0 + HALF_OUT  # -3.275
    edges = []
    for (x0, z0, x1, z1) in wells:
        edges.append(((x0, z0), (x0, z1)))  # west
        edges.append(((x1, z0), (x1, z1)))  # east
        if abs(z0 - face_z) > 1e-9:
            edges.append(((x0, z0), (x1, z0)))  # south
        edges.append(((x0, z1), (x1, z1)))  # north
    # drop edges fully internal to the merged opening (both sides inside wells)
    def inside_well(x, z, wells):
        return any(x0 - 1e-9 < x < x1 - 1e-9 and z0 - 1e-9 < z < z1 - 1e-9
                   for (x0, z0, x1, z1) in wells)

    kept = []
    # split edges where another well's span crosses them, so the midpoint
    # probe below sees each homogeneous piece separately
    split = []
    for e in edges:
        (ax, az), (bx, bz) = e
        cuts = set()
        for (x0, z0, x1, z1) in wells:
            if abs(az - bz) < 1e-9:  # horizontal edge at z = az
                for xc in (x0, x1):
                    if min(ax, bx) + 1e-9 < xc < max(ax, bx) - 1e-9 and z0 - 1e-9 < az < z1 + 1e-9:
                        cuts.add(xc)
            else:
                for zc in (z0, z1):
                    if min(az, bz) + 1e-9 < zc < max(az, bz) - 1e-9 and x0 - 1e-9 < ax < x1 + 1e-9:
                        cuts.add(zc)
        if not cuts:
            split.append(e)
            continue
        pts = sorted(cuts)
        if abs(az - bz) < 1e-9:
            bounds = [min(ax, bx)] + pts + [max(ax, bx)]
            split.extend((((lo, az), (hi, az))) for lo, hi in zip(bounds, bounds[1:]))
        else:
            bounds = [min(az, bz)] + pts + [max(az, bz)]
            split.extend((((ax, lo), (ax, hi))) for lo, hi in zip(bounds, bounds[1:]))
    for e in split:
        (ax, az), (bx, bz) = e
        # sample midpoints of the "outside" side of the edge; if that side is
        # inside another well, the edge is internal to the merged opening
        mx, mz = (ax + bx) / 2, (az + bz) / 2
        if abs(az - bz) < 1e-9:  # horizontal: probe z +/- eps
            if inside_well(mx, mz - 0.03, wells) and inside_well(mx, mz + 0.03, wells):
                continue
        else:  # vertical: probe x
            if inside_well(mx - 0.03, mz, wells) and inside_well(mx + 0.03, mz, wells):
                continue
        kept.append(e)
    # merge collinear touching/overlapping runs
    def merge_runs(edges):
        horiz = defaultdict(list)
        vert = defaultdict(list)
        for (a, b) in edges:
            if abs(a[1] - b[1]) < 1e-9:
                horiz[a[1]].append((min(a[0], b[0]), max(a[0], b[0])))
            else:
                vert[a[0]].append((min(a[1], b[1]), max(a[1], b[1])))
        out = []
        for z, ivs in horiz.items():
            for lo, hi in merge_intervals(ivs):
                out.append(((lo, z), (hi, z)))
        for x, ivs in vert.items():
            for lo, hi in merge_intervals(ivs):
                out.append(((x, lo), (x, hi)))
        return out

    def merge_intervals(ivs):
        ivs = sorted(ivs)
        out = []
        for lo, hi in ivs:
            if out and lo <= out[-1][1] + 1e-9:
                out[-1] = (out[-1][0], max(out[-1][1], hi))
            else:
                out.append((lo, hi))
        return out

    return merge_runs(kept)

# ---------------------------------------------------------------- openings
# (storey, vertical, line coord, s0, s1, y0, y1, kind, type, id)

OPENINGS = [
    # ground windows (sill 0.9, head 2.4)
    ("ground", False, Z1, -3.5, -2.5, 0.9, 2.4, "window", "win_flat"),
    ("ground", False, Z1, -1.5, -0.5, 0.9, 2.4, "window", "win_flat"),
    ("ground", False, Z1, 0.5, 1.5, 0.9, 2.4, "window", "win_flat"),
    ("ground", False, Z1, 2.5, 3.5, 0.9, 2.4, "window", "win_flat"),
    ("ground", False, Z0, -2.6, -1.6, 0.9, 2.4, "window", "win_flat"),
    ("ground", False, Z0, 0.6, 1.6, 0.9, 2.4, "window", "win_flat"),
    ("ground", False, Z0, 2.5, 3.5, 0.9, 2.4, "window", "win_flat"),
    ("ground", True, X0, 0.6, 1.6, 0.9, 2.4, "window", "win_flat"),
    ("ground", True, X0, -2.6, -1.6, 0.9, 2.4, "window", "win_flat"),
    ("ground", True, X1, 0.6, 1.6, 0.9, 2.4, "window", "win_flat"),
    ("ground", True, X1, -2.6, -1.6, 0.9, 2.4, "window", "win_flat"),
    # ground doors
    ("ground", False, Z1, -2.3, -1.3, 0.0, 2.1, "door", "door_plain"),   # entrance
    ("ground", True, 0.0, 2.0, 3.0, 0.0, 2.1, "door", "door_plain"),     # hall|parlour
    ("ground", False, -0.5, -2.1, -1.1, 0.0, 2.1, "door", "door_plain"), # hall|stairwell
    ("ground", False, -0.5, 0.8, 1.7, 0.0, 2.1, "door", "door_plain"),   # parlour|corridor_g
    ("ground", True, 2.0, -2.6, -1.6, 0.0, 2.1, "door", "door_plain"),   # corridor_g|pantry
    ("ground", True, 0.0, -1.8, -0.8, 0.0, 2.1, "door", "door_plain"),   # stairwell|corridor_g
    # upper windows
    ("upper", False, Z1, -3.5, -2.5, 0.9, 2.4, "window", "win_flat"),
    ("upper", False, Z1, -1.5, -0.5, 0.9, 2.4, "window", "win_flat"),
    ("upper", False, Z1, 0.5, 1.5, 0.9, 2.4, "window", "win_flat"),
    ("upper", False, Z1, 2.5, 3.5, 0.9, 2.4, "window", "win_flat"),
    ("upper", False, Z0, -3.0, -2.2, 0.9, 2.4, "window", "win_flat"),
    ("upper", False, Z0, 2.8, 3.5, 0.9, 2.4, "window", "win_flat"),
    ("upper", True, X0, 0.6, 1.6, 0.9, 2.4, "window", "win_flat"),
    ("upper", True, X0, 2.0, 3.0, 0.9, 2.4, "window", "win_flat"),
    ("upper", True, X0, -1.8, -1.0, 0.9, 2.4, "window", "win_flat"),
    ("upper", True, X1, 0.6, 1.6, 0.9, 2.4, "window", "win_flat"),
    ("upper", True, X1, 2.0, 3.0, 0.9, 2.4, "window", "win_flat"),
    ("upper", True, X1, -2.6, -1.6, 0.9, 2.4, "window", "win_flat"),
    # upper doors
    ("upper", False, -0.5, -2.2, -1.2, 0.0, 2.1, "door", "door_plain"),  # bed1
    ("upper", False, -0.5, 1.0, 2.0, 0.0, 2.1, "door", "door_plain"),    # bed2
    ("upper", True, -2.5, -1.8, -0.8, 0.0, 2.1, "door", "door_plain"),   # storage
    # attic gable windows (sill 0.6, head 2.1)
    ("attic", True, X1, 0.5, 2.0, 0.6, 2.1, "window", "win_flat"),
    ("attic", True, X1, -2.0, -0.5, 0.6, 2.1, "window", "win_flat"),
    ("attic", True, X0, 1.0, 2.0, 0.6, 2.1, "window", "win_flat"),
    # attic doors
    ("attic", True, 0.0, 1.0, 2.0, 0.0, 2.1, "door", "door_plain"),      # corridor_a|bed3
    ("attic", False, -1.0, -2.1, -1.2, 0.0, 2.1, "door", "door_plain"),  # corridor_a|storage2
]

# ---------------------------------------------------------------- space regions (axis coords)
# Floor regions per space before wall-face insets; wells subtracted.

SPACE_REGIONS = {
    "hall": [(-4.0, -0.5, 0.0, 3.5)],
    "parlour": [(0.0, -0.5, 4.0, 3.5)],
    "stairwell": [(-4.0, -3.5, 0.0, -0.5)],
    "corridor_g": [(0.0, -3.5, 2.0, -0.5)],
    "pantry": [(2.0, -3.5, 4.0, -0.5)],
    "bed1": [(-4.0, -0.5, 0.0, 3.5)],
    "bed2": [(0.0, -0.5, 4.0, 3.5)],
    "corridor_u": [(-2.5, -3.5, 4.0, -0.5), (-4.0, -3.5, -2.5, -2.5)],
    "storage": [(-4.0, -2.5, -2.5, -0.5)],
    "bed3": [(0.0, -3.0, 4.0, 3.0)],
    "corridor_a": [(-4.0, -1.0, 0.0, 3.0)],
    "storage2": [(-4.0, -3.0, 0.0, -1.0)],
}

SPACE_WELLS = {
    "corridor_u": [WELL_A_1, WELL_B_1],
    "corridor_a": [WELL_B_2],
    "storage2": [WELL_A_2, WELL_B_2],
}

# ---------------------------------------------------------------- rect algebra

def rect_subtract(rects, holes):
    """Rect list minus rect list (both [x0, z0, x1, z1]) via grid refinement."""
    xs = set()
    zs = set()
    for r in rects + holes:
        xs.add(r[0]); xs.add(r[2])
        zs.add(r[1]); zs.add(r[3])
    xs = sorted(xs); zs = sorted(zs)

    def inside(r, x, z):
        return r[0] - 1e-9 <= x < r[2] - 1e-9 and r[1] - 1e-9 <= z < r[3] - 1e-9

    cells = []
    for xi in range(len(xs) - 1):
        for zi in range(len(zs) - 1):
            cx = (xs[xi] + xs[xi + 1]) / 2
            cz = (zs[zi] + zs[zi + 1]) / 2
            if any(inside(r, cx, cz) for r in rects) and not any(inside(h, cx, cz) for h in holes):
                cells.append((xs[xi], zs[zi], xs[xi + 1], zs[zi + 1]))
    # merge vertically then horizontally
    changed = True
    while changed:
        changed = False
        for a in list(cells):
            for b in list(cells):
                if a == b:
                    continue
                if a[0] == b[0] and a[2] == b[2] and abs(a[3] - b[1]) < 1e-9:
                    cells.remove(a); cells.remove(b)
                    cells.append((a[0], a[1], a[2], b[3]))
                    changed = True
                    break
                if a[1] == b[1] and a[3] == b[3] and abs(a[2] - b[0]) < 1e-9:
                    cells.remove(a); cells.remove(b)
                    cells.append((a[0], a[1], b[2], a[3]))
                    changed = True
                    break
            if changed:
                break
    return cells

def trace_outline(rects):
    """Rectilinear CCW-from-above (negative-area) boundary of a rect union."""
    edges = []  # (a, b) directed so the region is on the... consistent winding
    for (x0, z0, x1, z1) in rects:
        # CCW seen from +Y == negative signed area in xz
        edges.append(((x0, z0), (x0, z1)))
        edges.append(((x0, z1), (x1, z1)))
        edges.append(((x1, z1), (x1, z0)))
        edges.append(((x1, z0), (x0, z0)))
    # cancel anti-parallel overlaps on shared lines
    def cancel(edges):
        # group by line
        horiz = defaultdict(list)  # z -> [(x_lo, x_hi, dir)] dir +1: going +x
        vert = defaultdict(list)
        for (a, b) in edges:
            if abs(a[1] - b[1]) < 1e-12:
                z = a[1]
                lo, hi = min(a[0], b[0]), max(a[0], b[0])
                horiz[z].append((lo, hi, 1 if b[0] > a[0] else -1))
            else:
                x = a[0]
                lo, hi = min(a[1], b[1]), max(a[1], b[1])
                vert[x].append((lo, hi, 1 if b[1] > a[1] else -1))

        def subtract_intervals(pos, neg):
            # pos: sorted [(lo,hi)], neg: [(lo,hi)] -> pos minus neg
            out = []
            for (lo, hi) in pos:
                cur = [(lo, hi)]
                for (nlo, nhi) in neg:
                    nxt = []
                    for (clo, chi) in cur:
                        if nhi <= clo or nlo >= chi:
                            nxt.append((clo, chi))
                        else:
                            if clo < nlo:
                                nxt.append((clo, nlo))
                            if nhi < chi:
                                nxt.append((nhi, chi))
                    cur = nxt
                out.extend(cur)
            return out

        result = []
        for z, ivs in horiz.items():
            pos = sorted((lo, hi) for (lo, hi, d) in ivs if d > 0)
            neg = [(lo, hi) for (lo, hi, d) in ivs if d < 0]
            neg2 = [(lo, hi) for (lo, hi, d) in ivs if d < 0]
            for (lo, hi) in subtract_intervals(pos, neg):
                result.append(((lo, z), (hi, z)))
            for (lo, hi) in subtract_intervals(sorted(neg2), pos):
                result.append(((hi, z), (lo, z)))
        for x, ivs in vert.items():
            pos = sorted((lo, hi) for (lo, hi, d) in ivs if d > 0)
            neg = [(lo, hi) for (lo, hi, d) in ivs if d < 0]
            for (lo, hi) in subtract_intervals(pos, neg):
                result.append(((x, lo), (x, hi)))
            for (lo, hi) in subtract_intervals(sorted(neg), pos):
                result.append(((x, hi), (x, lo)))
        return result

    edges = cancel(edges)
    # chain into loops
    loops = []
    adj = defaultdict(list)
    for e in edges:
        adj[e[0]].append(e)
    while edges:
        e = edges.pop()
        loop = [e[0], e[1]]
        while loop[-1] != loop[0]:
            nxt = None
            for cand in adj[loop[-1]]:
                if cand in edges:
                    nxt = cand
                    break
            if nxt is None:
                raise RuntimeError(f"outline trace failed at {loop[-1]}")
            edges.remove(nxt)
            loop.append(nxt[1])
        loop.pop()
        loops.append(loop)
    if len(loops) != 1:
        raise RuntimeError(f"space outline is not a single loop ({len(loops)})")
    loop = loops[0]
    # drop collinear vertices
    simplified = []
    n = len(loop)
    for k in range(n):
        p, c, q2 = loop[(k - 1) % n], loop[k], loop[(k + 1) % n]
        cross = (c[0] - p[0]) * (q2[1] - c[1]) - (c[1] - p[1]) * (q2[0] - c[0])
        if abs(cross) > 1e-9:
            simplified.append(c)
    return simplified

# ---------------------------------------------------------------- build

def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "house_b0.ir.json"

    storeys = [
        {"id": "ground", "kind": "ground", "kind_code": 1, "y": q(0.0), "h": q(H_GROUND), "slab_t": q(SLAB_T)},
        {"id": "upper", "kind": "upper", "kind_code": 2, "y": q(Y_UPPER), "h": q(H_UPPER), "slab_t": q(SLAB_T)},
        {"id": "attic", "kind": "attic", "kind_code": 3, "y": q(Y_ATTIC), "h": q(H_EAVE), "slab_t": q(0.0)},
    ]

    outline = [(-OUT_X, -OUT_Z), (-OUT_X, OUT_Z), (OUT_X, OUT_Z), (OUT_X, -OUT_Z)]
    volumes = [{
        "id": "main",
        "outline": [v2(*p) for p in outline],
        "storeys": [0, 2],
        "roof": {
            "kind": "gable", "kind_code": 1,
            "pitch_by_edge": [q(90.0), q(PITCH), q(90.0), q(PITCH)],
            "overhang": q(OVERHANG),
            "eave_rise": q(H_EAVE),
            "eave_y": q(EAVE_Y),
            "roof_t": q(ROOF_T),
        },
    }]

    roof = {
        "planes": [
            {"volume": "main", "edge": 1, "pitch": q(PITCH),
             "o": v3(*PLANE_F["o"]), "n": v3(*PLANE_F["n"])},
            {"volume": "main", "edge": 3, "pitch": q(PITCH),
             "o": v3(*PLANE_B["o"]), "n": v3(*PLANE_B["n"])},
        ],
        "ridges": [{"a": v3(-RIDGE_HALF, RIDGE_Y, 0.0), "b": v3(RIDGE_HALF, RIDGE_Y, 0.0)}],
        "valleys": [],
        "hips": [],
    }

    # ---- walls, openings, railings, nodes
    all_walls = []
    all_railings = []
    all_nodes = []
    wall_seq = defaultdict(int)
    op_seq = defaultdict(int)
    door_ids = {}  # (storey, vertical, coord, s0) -> opening id

    for storey in ("ground", "upper", "attic"):
        walls = build_walls(storey)
        # openings onto wall runs
        ops = [o for o in OPENINGS if o[0] == storey]
        assigned = defaultdict(list)
        for op in ops:
            _, vertical, coord, s0, s1, y0, y1, kind, otype = op
            best = None
            for w in walls:
                if w["vertical"] != vertical or abs(w["coord"] - coord) > 1e-9:
                    continue
                if w["lo"] - 1e-9 <= s0 and s1 <= w["hi"] + 1e-9:
                    best = w
                    break
            if best is None:
                raise RuntimeError(f"opening {op} fits no wall on {storey}")
            assigned[id(best)].append(op)

        for w in walls:
            idx = wall_seq[storey]
            wall_seq[storey] += 1
            wid = f"w_{storey[0]}_{idx:03d}"
            entry = {
                "id": wid,
                "storey": storey,
                "a": v2(*w["a"]),
                "b": v2(*w["b"]),
                "class": w["class"], "class_code": w["class_code"],
                "thickness": q(w["thickness"]),
                "owner": min((s for s in (w["side_l"], w["side_r"])
                              if s not in ("outside", "void", "well")), default="main"),
                "side_l": w["side_l"],
                "side_r": w["side_r"],
            }
            if storey == "attic":
                if w["class"] == "gable":
                    entry["h"] = q(5.0)
                    entry["top_profile"] = [plane_json(CEIL_F), plane_json(CEIL_B)]
                elif w["class"] == "knee":
                    entry["h"] = q(2.0)
                    entry["top_profile"] = [plane_json(CEIL_F if w["coord"] > 0 else CEIL_B)]
                elif w["class"] == "partition":
                    entry["h"] = q(5.0)
                    entry["top_profile"] = [plane_json(CEIL_F), plane_json(CEIL_B)]
                else:
                    entry["h"] = q(H_EAVE)
            else:
                entry["h"] = q(STOREY_H[storey])
            ops_json = []
            for op in sorted(assigned.get(id(w), []), key=lambda o: o[3]):
                _, vertical, coord, s0, s1, y0, y1, kind, otype = op
                oi = op_seq[storey]
                op_seq[storey] += 1
                oid = f"{'d' if kind == 'door' else 'w'}_{storey[0]}_{oi:03d}"
                if kind == "door":
                    door_ids[(storey, vertical, q(coord), q(s0))] = oid
                # s along the wall from a
                if w["vertical"]:
                    s_a, s_b = s0 - w["a"][1], s1 - w["a"][1]
                else:
                    s_a, s_b = s0 - w["a"][0], s1 - w["a"][0]
                ops_json.append({
                    "id": oid,
                    "kind": kind, "kind_code": 1 if kind == "door" else 2,
                    "type": otype,
                    "s0": q(s_a), "s1": q(s_b), "y0": q(y0), "y1": q(y1),
                })
            if ops_json:
                entry["openings"] = ops_json
            all_walls.append(entry)

        for ri, (a, b) in enumerate(sorted(build_railings(storey))):
            all_railings.append({
                "id": f"r_{storey[0]}_{ri:02d}",
                "storey": storey,
                "a": v2(*a), "b": v2(*b),
                "h": q(0.9),
            })

        # nodes at wall endpoints and junctions (ground/upper: all wall tops
        # are flat and equal-height, so a simple post fills the corner)
        if storey != "attic":
            pts = defaultdict(list)  # (x, z) -> [wall]
            for w in walls:
                pts[w["a"]].append(w)
                pts[w["b"]].append(w)
            ni = 0
            for p in sorted(pts):
                inc = pts[p]
                faces = {}
                for w in inc:
                    for end in ("a", "b"):
                        if w[end] == p:
                            other = w["b"] if end == "a" else w["a"]
                            d = (other[0] - p[0], other[1] - p[1])
                            L = math.hypot(*d)
                            d = (round(d[0] / L), round(d[1] / L))
                            faces[d] = max(faces.get(d, 0.0), w["h"] if storey == "attic" else STOREY_H[storey])
                all_nodes.append({
                    "id": f"n_{storey[0]}_{ni:02d}",
                    "storey": storey,
                    "p": v2(*p),
                    "owner": min(w["owner"] if "owner" in w else "main" for w in inc),
                    "faces": [{"dir": v2(*d), "h": q(h)} for d, h in sorted(faces.items())],
                })
                ni += 1

    # ---- spaces: cells, floor rects, outline, area
    spaces = []
    for sid, (storey, fn, fn_code, zone, zone_code, habitable, hearth_code) in sorted(SPACE_META.items()):
        lab = LABELS[storey]
        cells = [[i, j] for i in range(NX) for j in range(NZ) if lab[i][j] == sid]
        region = [list(r) for r in SPACE_REGIONS[sid]]
        holes = [list(h) for h in SPACE_WELLS.get(sid, [])]
        rects = rect_subtract(region, holes) if holes else [tuple(r) for r in region]
        # inset only at the outside boundary (0.225); floors run under interior walls
        inset_rects = []
        for (x0, z0, x1, z1) in rects:
            if abs(x0 - X0) < 1e-9:
                x0 += HALF_OUT
            if abs(x1 - X1) < 1e-9:
                x1 -= HALF_OUT
            if abs(z0 - Z0) < 1e-9:
                z0 += HALF_OUT
            if abs(z1 - Z1) < 1e-9:
                z1 -= HALF_OUT
            if x1 - x0 > 1e-6 and z1 - z0 > 1e-6:
                inset_rects.append((x0, z0, x1, z1))
        if storey == "attic":
            area = 0.0
            for (x0, z0, x1, z1) in inset_rects:
                zl = max(z0, -Z_USEFUL)
                zr = min(z1, Z_USEFUL)
                if zr > zl:
                    area += (x1 - x0) * (zr - zl)
        else:
            area = sum((x1 - x0) * (z1 - z0) for (x0, z0, x1, z1) in inset_rects)
        outline = trace_outline(inset_rects)
        entry = {
            "id": sid,
            "storey": storey,
            "fn": fn, "fn_code": fn_code,
            "zone": zone, "zone_code": zone_code,
            "cells": cells,
            "outline": [v2(*p) for p in outline],
            "area_net": q(area),
            "habitable": habitable,
        }
        if hearth_code:
            entry["hearth"] = "fireplace"
            entry["hearth_code"] = hearth_code
        spaces.append(entry)

    # ---- slabs
    def slab_rects(wells):
        base = [(X0, Z0, X1, Z1)]
        holes = list(wells) + [(CHIM_X0, CHIM_Z0, CHIM_X1, CHIM_Z1)]
        return rect_subtract(base, [list(h) for h in holes])

    slabs = [
        {"storey_below": "ground", "storey_above": "upper",
         "rects": [{"x0": q(r[0]), "z0": q(r[1]), "x1": q(r[2]), "z1": q(r[3])}
                   for r in sorted(slab_rects([WELL_A_1, WELL_B_1]))],
         "y": q(H_GROUND), "t": q(SLAB_T)},
        {"storey_below": "upper", "storey_above": "attic",
         "rects": [{"x0": q(r[0]), "z0": q(r[1]), "x1": q(r[2]), "z1": q(r[3])}
                   for r in sorted(slab_rects([WELL_A_2, WELL_B_2]))],
         "y": q(H_GROUND + SLAB_T + H_UPPER), "t": q(SLAB_T)},
    ]

    # ---- stairs
    stairs = [
        {
            "id": "stair1", "type": "l", "type_code": 2,
            "from": "ground", "to": "upper",
            "start": v2(ST_M1_X0, (ST_Z0 + ST_Z1) / 2),
            "dir": v2(1.0, 0.0),
            "width": q(1.0),
            "risers": 16, "rise": q((H_GROUND + SLAB_T) / 16), "going": q(0.25),
            "risers_low": 9,
            "dir2": v2(0.0, 1.0),
            "landing": [v2(ST_LAND_X0, ST_Z0), v2(ST_LAND_X1, ST_Z1)],
            "well": [[q(WELL_A_1[0]), q(WELL_A_1[1]), q(WELL_A_1[2]), q(WELL_A_1[3])],
                     [q(WELL_B_1[0]), q(WELL_B_1[1]), q(WELL_B_1[2]), q(WELL_B_1[3])]],
        },
        {
            "id": "stair2", "type": "l", "type_code": 2,
            "from": "upper", "to": "attic",
            "start": v2(ST_M1_X0, (ST_Z0 + ST_Z1) / 2),
            "dir": v2(1.0, 0.0),
            "width": q(1.0),
            "risers": 16, "rise": q((H_UPPER + SLAB_T) / 16), "going": q(0.25),
            "risers_low": 9,
            "dir2": v2(0.0, 1.0),
            "landing": [v2(ST_LAND_X0, ST_Z0), v2(ST_LAND_X1, ST_Z1)],
            "well": [[q(WELL_A_2[0]), q(WELL_A_2[1]), q(WELL_A_2[2]), q(WELL_A_2[3])],
                     [q(WELL_B_2[0]), q(WELL_B_2[1]), q(WELL_B_2[2]), q(WELL_B_2[3])]],
        },
    ]

    # ---- chimney
    hall_parlour_wall = None
    for w in all_walls:
        if w["storey"] == "ground" and w["a"][0] == 0.0 and w["b"][0] == 0.0 \
                and {w["side_l"], w["side_r"]} == {"hall", "parlour"}:
            hall_parlour_wall = w["id"]
    if hall_parlour_wall is None:
        raise RuntimeError("hall|parlour wall not found")
    chim_cells = sorted([list(cell_of(CHIM_X0 + 0.01, 0.25)), list(cell_of(CHIM_X1 - 0.01, 0.25))])
    chimneys = [{
        "id": "chimney1",
        "hearths": [
            {"space": "hall", "kind": "fireplace", "kind_code": 1,
             "wall": hall_parlour_wall, "s": q(0.75)},
            {"space": "parlour", "kind": "fireplace", "kind_code": 1,
             "wall": hall_parlour_wall, "s": q(0.75)},
        ],
        "section": v2(CHIM_X1 - CHIM_X0, CHIM_Z1 - CHIM_Z0),
        "cells_by_storey": {s: chim_cells for s in ("ground", "upper", "attic")},
        "top": v3(0.0, CHIM_TOP_Y, (CHIM_Z0 + CHIM_Z1) / 2),
        "roof_plane": 0,
    }]

    # ---- access
    def door(storey, vertical, coord, s0):
        return door_ids[(storey, vertical, q(coord), q(s0))]

    access = {
        "nodes": ["outside"] + sorted(SPACE_META),
        "edges": [
            {"a": "outside", "b": "hall", "via": "door", "ref": door("ground", False, Z1, -2.3)},
            {"a": "hall", "b": "parlour", "via": "door", "ref": door("ground", True, 0.0, 2.0)},
            {"a": "hall", "b": "stairwell", "via": "door", "ref": door("ground", False, -0.5, -2.1)},
            {"a": "parlour", "b": "corridor_g", "via": "door", "ref": door("ground", False, -0.5, 0.8)},
            {"a": "corridor_g", "b": "pantry", "via": "door", "ref": door("ground", True, 2.0, -2.6)},
            {"a": "corridor_g", "b": "stairwell", "via": "door", "ref": door("ground", True, 0.0, -1.8)},
            {"a": "stairwell", "b": "corridor_u", "via": "stair", "ref": "stair1"},
            {"a": "corridor_u", "b": "bed1", "via": "door", "ref": door("upper", False, -0.5, -2.2)},
            {"a": "corridor_u", "b": "bed2", "via": "door", "ref": door("upper", False, -0.5, 1.0)},
            {"a": "corridor_u", "b": "storage", "via": "door", "ref": door("upper", True, -2.5, -1.8)},
            {"a": "corridor_u", "b": "corridor_a", "via": "stair", "ref": "stair2"},
            {"a": "corridor_a", "b": "bed3", "via": "door", "ref": door("attic", True, 0.0, 1.0)},
            {"a": "corridor_a", "b": "storage2", "via": "door", "ref": door("attic", False, -1.0, -2.1)},
        ],
    }

    anchors = [
        {"kind": "entrance", "kind_code": 1, "p": v3(-1.8, 0.0, 4.1), "dir": v3(0, 0, 1), "space": "hall"},
        {"kind": "hearth", "kind_code": 2, "p": v3(-0.9, 0.0, 0.25), "dir": v3(-1, 0, 0), "space": "hall"},
        {"kind": "hearth", "kind_code": 2, "p": v3(0.9, 0.0, 0.25), "dir": v3(1, 0, 0), "space": "parlour"},
        {"kind": "bed_slot", "kind_code": 3, "p": v3(-3.0, Y_UPPER, 2.0), "dir": v3(1, 0, 0), "space": "bed1"},
        {"kind": "bed_slot", "kind_code": 3, "p": v3(3.0, Y_UPPER, 2.0), "dir": v3(-1, 0, 0), "space": "bed2"},
        {"kind": "bed_slot", "kind_code": 3, "p": v3(3.0, Y_ATTIC, 2.0), "dir": v3(-1, 0, 0), "space": "bed3"},
    ]

    ir = {
        "format": "building-topology-generator-ir/0",
        "seed": 0,
        "program": "frozen_house_b0",
        "params": {
            "cell": q(CELL), "h_ground": q(H_GROUND), "h_upper": q(H_UPPER),
            "slab_t": q(SLAB_T), "t_exterior": q(T_EXTERIOR), "t_bearing": q(T_BEARING),
            "t_partition": q(T_PARTITION), "t_knee": q(T_KNEE),
            "player_height": q(1.8), "player_radius": q(0.3), "headroom_margin": q(0.2),
            "glass_ratio": q(0.125), "h_knee": q(1.2), "roof_t": q(ROOF_T),
            "h_above_roof": q(0.5), "valley_margin": q(0.6),
        },
        "storeys": storeys,
        "volumes": volumes,
        "roof": roof,
        "spaces": spaces,
        "walls": sorted(all_walls, key=lambda w: w["id"]),
        "nodes": sorted(all_nodes, key=lambda n: n["id"]),
        "slabs": slabs,
        "railings": sorted(all_railings, key=lambda r: r["id"]),
        "stairs": stairs,
        "chimneys": chimneys,
        "dormers": [],
        "access": access,
        "anchors": anchors,
        "report": {"cost": {}, "moves": []},
    }

    # opening ids in access edges must exist
    op_ids = {o["id"] for w in all_walls for o in w.get("openings", [])}
    for e in access["edges"]:
        if e["via"] == "door" and e["ref"] not in op_ids:
            raise RuntimeError(f"access ref {e['ref']} not an opening")

    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(ir, f, indent=2, ensure_ascii=False)
        f.write("\n")

    print(f"wrote {out_path}")
    print(f"  walls {len(all_walls)} (g/u/a: "
          f"{sum(1 for w in all_walls if w['storey'] == 'ground')}/"
          f"{sum(1 for w in all_walls if w['storey'] == 'upper')}/"
          f"{sum(1 for w in all_walls if w['storey'] == 'attic')}), "
          f"nodes {len(all_nodes)}, railings {len(all_railings)}, "
          f"openings {sum(len(w.get('openings', [])) for w in all_walls)}")
    for s in spaces:
        print(f"  {s['id']:<10} area_net {s['area_net']:>8}  cells {len(s['cells']):>2}  "
              f"outline {len(s['outline'])} pts")
    for sl in slabs:
        print(f"  slab {sl['storey_below']}->{sl['storey_above']}: {len(sl['rects'])} rects")

if __name__ == "__main__":
    main()
