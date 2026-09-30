#!/usr/bin/env python3
"""bake_floor.py Scene xmin xmax ymin ymax zmin zmax out.bin
Walkable triangles (upward normals) from renderers + terrain, in the game's fixed DS frame
(origin ORIGIN = cabin centre 315,45.25,327; x=wx-ox, y=wy-oy, z=-(wz-oz); 20.12), bucketed on a 1 m grid.
Format: 'FLR1', s32 gx0, gz0 (20.12, cell (0,0) corner), u32 nx, nz (0.25 m cells), u16 starts[nx*nz+1], s16 heights (1/256 m, DS frame)."""
import sys, os, struct
import numpy as np
import unity, bake_level, terrain
ORIGIN = np.array([float(v) for v in os.environ["PIVOT"].split(",")]) if os.environ.get("PIVOT") else np.array([315.0, 45.25, 327.0])

def run(scn, lo, hi, out):
    lo = np.array(lo); hi = np.array(hi)
    s = unity.Scene(scn)
    for g in s.go.values():
        if g.get("name") in os.environ.get("FORCE", "").split(","): g["active"] = 1
    items, _ = bake_level.collect(s, lo, hi)
    tris = [t for _, t, _ in items]
    for _, t, _ in terrain.terrain_tris(lo[0], hi[0], lo[2], hi[2], terrain.SCALE[0]): tris.append(t)
    T = np.concatenate(tris)
    n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
    ln = np.linalg.norm(n, axis=1); ok = ln > 1e-9
    T, n, ln = T[ok], n[ok], ln[ok]
    ny = n[:, 1] / ln
    T, ln = T[ny > 0.55], ln[ny > 0.55]   # unity front faces: cross(b-a,c-a) points out
    T = T[ln > float(os.environ.get("MINAREA", 0.002)) * 2]
    print(len(T), "walkable tris")
    # DS frame, rasterize to a 0.25 m multi-layer heightfield
    D = T - ORIGIN; D[:, :, 2] *= -1
    C = 0.25
    g0 = np.floor(np.array([lo[0] - ORIGIN[0], -(hi[2] - ORIGIN[2])]))
    nx = int(np.ceil((hi[0] - lo[0]) / C)) + 1; nz = int(np.ceil((hi[2] - lo[2]) / C)) + 1
    cells = [[] for _ in range(nx * nz)]
    for t in D:
        xz = t[:, [0, 2]]
        a = np.floor((xz.min(0) - g0) / C).astype(int); b = np.floor((xz.max(0) - g0) / C).astype(int)
        a = np.maximum(a, 0); b = np.minimum(b, [nx - 1, nz - 1])
        if a[0] > b[0] or a[1] > b[1]: continue
        gx, gz = np.meshgrid(np.arange(a[0], b[0] + 1), np.arange(a[1], b[1] + 1))
        px = g0[0] + (gx.ravel() + 0.5) * C; pz = g0[1] + (gz.ravel() + 0.5) * C
        (x0, z0), (x1, z1), (x2, z2) = xz
        den = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2)
        if abs(den) < 1e-12: continue
        w0 = ((z1 - z2) * (px - x2) + (x2 - x1) * (pz - z2)) / den
        w1 = ((z2 - z0) * (px - x2) + (x0 - x2) * (pz - z2)) / den
        w2 = 1 - w0 - w1
        e = -0.02
        ins = (w0 >= e) & (w1 >= e) & (w2 >= e)
        y = w0 * t[0, 1] + w1 * t[1, 1] + w2 * t[2, 1]
        for cx, cz, yy in zip(gx.ravel()[ins], gz.ravel()[ins], y[ins]): cells[cz * nx + cx].append(yy)
    starts = [0]; hs = []
    for c in cells:
        c = sorted(c); m = []
        for y in c:
            if m and y - m[-1] < 0.35: m[-1] = y      # merge close surfaces, keep the top one
            else: m.append(y)
        hs += m; starts.append(len(hs))
    assert len(hs) < 65536, len(hs)
    q = np.round(np.array(hs) * 256).astype("<i2")      # 1/256 m
    with open(out, "wb") as f:
        f.write(b"FLR1" + struct.pack("<iiII", int(g0[0] * 4096), int(g0[1] * 4096), nx, nz))
        f.write(np.array(starts, "<u2").tobytes()); f.write(q.tobytes())
    print(nx, "x", nz, "cells", len(hs), "heights", os.path.getsize(out), "bytes")

if __name__ == "__main__":
    a = sys.argv
    run(a[1], [float(x) for x in a[2:8:2]], [float(x) for x in a[3:9:2]], a[8])
