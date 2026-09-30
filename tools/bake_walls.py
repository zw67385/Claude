#!/usr/bin/env python3
"""bake_walls.py Scene xmin xmax ymin ymax zmin zmax out.bin  -- blocked-cell bitmap for walking scenes.
Steep renderer triangles whose surface passes through the body band (0.3..1.6 m above the lowest walkable
floor of the cell) block their 0.25 m cell. Frame: PIVOT (unity) -> DS x = wx-px, z = -(wz-pz), 20.12.
Format: 'WAL1', s32 gx0, gz0 (DS, cell (0,0) corner), u32 nx, nz, then nx*nz bits (row-major by DS z, LSB first).
env FORCE (names to activate), SKIP/ONLY (bake_level filters), PIVOT, WSKIP (name substrings that never block)."""
import sys, os, struct
import numpy as np
import unity, bake_level, terrain
C = 0.25

def run(scn, lo, hi, out):
    lo = np.array(lo); hi = np.array(hi)
    piv = np.array([float(v) for v in os.environ["PIVOT"].split(",")])
    s = unity.Scene(scn)
    for g in s.go.values():
        if g.get("name") in os.environ.get("FORCE", "").split(","): g["active"] = 1
    bake_level.NAMES.clear()
    items, _ = bake_level.collect(s, lo, hi)
    wskip = [w for w in os.environ.get("WSKIP", "").split(",") if w]
    names = bake_level.NAMES
    T = np.concatenate([t for _, t, _ in items])
    if os.environ.get("TERRAIN_ASSET"):
        tt = [t for _, t, _ in terrain.terrain_tris(lo[0], hi[0], lo[2], hi[2], terrain.SCALE[0])]
        if tt: T = np.concatenate([T] + tt)
    n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]); ln = np.linalg.norm(n, axis=1); ok = ln > 1e-9
    T, n, ln = T[ok], n[ok], ln[ok]; ny = n[:, 1] / ln
    nx_ = int(np.ceil((hi[0] - lo[0]) / C)); nz_ = int(np.ceil((hi[2] - lo[2]) / C))
    # lowest walkable floor per cell (unity frame, cell index from lo)
    flo = np.full((nz_, nx_), 1e9)
    def samples(tris, step):
        out = []
        for t in tris:
            a, b, c = t
            m = max(2, int(max(np.linalg.norm(b - a), np.linalg.norm(c - a), np.linalg.norm(c - b)) / step) + 1)
            u, v = np.meshgrid(np.linspace(0, 1, m), np.linspace(0, 1, m)); k = (u + v) <= 1
            u, v = u[k], v[k]
            out.append(a + np.outer(u, b - a) + np.outer(v, c - a))
        return np.concatenate(out) if out else np.zeros((0, 3))
    W = samples(T[ny > 0.55], C * 0.7)
    ix = ((W[:, 0] - lo[0]) / C).astype(int); iz = ((W[:, 2] - lo[2]) / C).astype(int)
    k = (ix >= 0) & (ix < nx_) & (iz >= 0) & (iz < nz_)
    np.minimum.at(flo, (iz[k], ix[k]), W[k, 1])
    base = np.where(flo < 1e8, flo, lo[1])
    S = T[np.abs(ny) < 0.5]
    P = samples(S, C * 0.5)
    ix = ((P[:, 0] - lo[0]) / C).astype(int); iz = ((P[:, 2] - lo[2]) / C).astype(int)
    k = (ix >= 0) & (ix < nx_) & (iz >= 0) & (iz < nz_)
    P, ix, iz = P[k], ix[k], iz[k]
    h = P[:, 1] - base[iz, ix]
    blk = np.zeros((nz_, nx_), bool)
    m = (h > 0.3) & (h < 1.6)
    blk[iz[m], ix[m]] = True
    # WCLEAR=x0,x1,z0,z1[;...] (unity): force walkable (door openings behind a solid wall mesh)
    for r in [r for r in os.environ.get("WCLEAR", "").split(";") if r]:
        x0, x1, z0, z1 = [float(v) for v in r.split(",")]
        blk[int((z0 - lo[2]) / C):int(np.ceil((z1 - lo[2]) / C)), int((x0 - lo[0]) / C):int(np.ceil((x1 - lo[0]) / C))] = False
    # DS frame rows: DS z = -(wz - pz); row r covers DS z from gz0 + r*C. Flip unity z order.
    blk_ds = blk[::-1]
    gx0 = lo[0] - piv[0]; gz0 = -(hi[2] - piv[2])
    # pad to whole cells: row 0 corresponds to unity z in [lo2 + (nz-1)C, lo2 + nz*C] -> DS z0 = -(lo2 + nz*C - pz)
    gz0 = -(lo[2] + nz_ * C - piv[2])
    bits = np.packbits(blk_ds.reshape(-1), bitorder="little")
    open(out, "wb").write(b"WAL1" + struct.pack("<iiII", int(gx0 * 4096), int(gz0 * 4096), nx_, nz_) + bits.tobytes())
    print("walls", out, nx_, "x", nz_, "blocked", int(blk.sum()))

if __name__ == "__main__":
    a = sys.argv
    run(a[1], [float(a[2]), float(a[4]), float(a[6])], [float(a[3]), float(a[5]), float(a[7])], a[8])
