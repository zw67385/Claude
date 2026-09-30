#!/usr/bin/env python3
"""bake_collide.py Scene xmin xmax ymin ymax zmin zmax out.bin  -- solid BoxColliders as AABBs (DS space, centre-relative, 20.12)."""
import sys, os, struct, itertools
import numpy as np
import unity
from bake_level import quat_mat
def run(scn, lo, hi, out):
    lo = np.array(lo); hi = np.array(hi); ctr = np.array([315, 45.25, 327.0]) if os.environ.get("ORIGIN") else np.array([float(v) for v in os.environ["PIVOT"].split(",")]) if os.environ.get("PIVOT") else (lo + hi) / 2
    s = unity.Scene(scn); boxes = []; nmesh = 0
    for g in s.go.values():
        if g.get("name") in os.environ.get("FORCE", "").split(","): g["active"] = 1
    for gf, g in s.go.items():
        if "tf" not in g or not s.active_in_hierarchy(gf): continue
        for cf, c, d in s.comps(gf):
            if c == 64: nmesh += 1
            if c != 65 or d.get("m_IsTrigger", 0) or not d.get("m_Enabled", 1): continue
            p, q, sc = s.world(g["tf"])
            cen = np.array([d["m_Center"][k] for k in "xyz"], float); size = np.array([d["m_Size"][k] for k in "xyz"], float)
            R = quat_mat(q)
            corners = [cen + size * np.array(sg) / 2 for sg in itertools.product((-1, 1), repeat=3)]
            wc = np.array([R @ (c_ * np.array(sc)) + np.array(p) for c_ in corners])
            mn, mx = wc.min(0), wc.max(0)
            if np.any(mx < lo) or np.any(mn > hi): continue
            mn = mn - ctr; mx = mx - ctr
            boxes.append((mn[0], mx[0], mn[1], mx[1], -mx[2], -mn[2]))
    b = b"".join(struct.pack("<6i", *[int(round(v * 4096)) for v in bx]) for bx in boxes)
    open(out, "wb").write(struct.pack("<I", len(boxes)) + b)
    print(len(boxes), "boxes,", nmesh, "mesh colliders (ignored)")
if __name__ == "__main__":
    a = sys.argv
    run(a[1], [float(x) for x in a[2:8:2]], [float(x) for x in a[3:9:2]], a[8])
