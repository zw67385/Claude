"""Visibility from a fixed eye point: rasterise every triangle into a z-buffered cube map around the eye
and keep only those that own at least one texel. Used for the RV cab, whose camera never leaves the
driver's seat, so the exterior skin and the rooms behind the seats cost nothing."""
import numpy as np

# cube faces: (forward, right, up)
FACES = [(np.array(f, float), np.array(r, float), np.array(u, float)) for f, r, u in (
    ((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
    ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0)))]
NEAR = 0.02


def _clip(poly):
    """clip a polygon (n,3 in face space, z = depth) against z >= NEAR"""
    out = []
    for i in range(len(poly)):
        a, b = poly[i], poly[(i + 1) % len(poly)]
        ia, ib = a[2] >= NEAR, b[2] >= NEAR
        if ia: out.append(a)
        if ia != ib:
            t = (NEAR - a[2]) / (b[2] - a[2]); out.append(a + (b - a) * t)
    return out


def visible(tris, eye, res=256):
    """tris (n,3,3) world positions -> bool (n,) seen from eye"""
    n = len(tris); seen = np.zeros(n, bool)
    rel = tris - np.asarray(eye, float)
    for f, r, u in FACES:
        zb = np.full((res, res), np.inf); idb = np.full((res, res), -1, np.int64)
        loc = np.stack([rel @ r, rel @ u, rel @ f], axis=-1)          # (n,3,3)
        cand = np.nonzero((loc[:, :, 2] > NEAR).any(axis=1))[0]
        for i in cand:
            poly = _clip(list(loc[i]))
            if len(poly) < 3: continue
            P = np.array(poly)
            sx = (P[:, 0] / P[:, 2] * 0.5 + 0.5) * res; sy = (0.5 - P[:, 1] / P[:, 2] * 0.5) * res
            iz = 1.0 / P[:, 2]
            x0 = max(int(np.floor(sx.min())), 0); x1 = min(int(np.ceil(sx.max())), res - 1)
            y0 = max(int(np.floor(sy.min())), 0); y1 = min(int(np.ceil(sy.max())), res - 1)
            if x0 > x1 or y0 > y1: continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            for k in range(1, len(P) - 1):     # fan
                ax, ay, bx, by, cx, cy = sx[0], sy[0], sx[k], sy[k], sx[k + 1], sy[k + 1]
                den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
                if abs(den) < 1e-12: continue
                w0 = ((by - cy) * (gx - cx) + (cx - bx) * (gy - cy)) / den
                w1 = ((cy - ay) * (gx - cx) + (ax - cx) * (gy - cy)) / den
                w2 = 1 - w0 - w1
                m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
                if not m.any(): continue
                z = 1.0 / (w0 * iz[0] + w1 * iz[k] + w2 * iz[k + 1])
                sub = zb[y0:y1 + 1, x0:x1 + 1]; sid = idb[y0:y1 + 1, x0:x1 + 1]
                w = m & (z < sub)
                sub[w] = z[w]; sid[w] = i
        ids = idb[idb >= 0]
        seen[np.unique(ids)] = True
    return seen
