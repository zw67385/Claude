#!/usr/bin/env python3
"""Bake a region of a Unity scene into DS display lists + textures.
usage: bake_level.py Scene xmin xmax ymin ymax zmin zmax out_dir [texsize]
Output: out_dir/level.bin (see pack format in README), coordinates in metres relative to bbox centre,
DS space (z flipped)."""
import sys, os, struct, collections
import numpy as np
from PIL import Image
import unity, meshio, matio, gx
G = unity.guid_map()
SCALE = float(os.environ.get("SCALE", 1))

def quat_mat(q):
    x, y, z, w = q
    return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                     [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                     [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])

def texfile(guid):
    if guid.startswith("/"): return guid       # generated image (e.g. tree impostor crop)
    p = G.get(guid)
    return os.path.join(unity.ROOT, p) if p else None


import meshoptimizer
def decimate(tp, uv, target):
    """tp (n,3,3), uv (n,3,2) triangle soup -> decimated soup."""
    n = len(tp)
    if n <= target: return tp, uv
    flat = np.concatenate([tp.reshape(-1,3), uv.reshape(-1,2)], axis=1)
    key = np.round(flat*np.array([1e3,1e3,1e3,1e2,1e2])).astype(np.int64)
    _, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
    verts = flat[first].astype(np.float32); idx = inv.reshape(-1).astype(np.uint32)
    pos = np.ascontiguousarray(verts[:, :3])
    dst = np.zeros(len(idx), dtype=np.uint32)
    k = meshoptimizer.simplify(dst, idx, pos, target_index_count=int(target*3), target_error=2.0, options=0)
    if k > target*3*1.5:
        k = meshoptimizer.simplify_sloppy(dst, idx, pos, target_index_count=int(target*3), target_error=2.0)
    t = dst[:k].reshape(-1,3)
    if len(t)==0: return tp[:0], uv[:0]
    return verts[t][:,:,:3].astype(np.float64), verts[t][:,:,3:]

_tex = {}
def conv_tex(guid, size, allow_cut=True):
    """returns (index-key, w, h, pal(list of 15-bit), data bytes 8bpp) or None"""
    if (guid, size, allow_cut) in _tex: return _tex[(guid, size, allow_cut)]
    p = texfile(guid); r = None
    try:
        im = Image.open(p).convert("RGBA")
        a = np.array(im)[:, :, 3]
        w, h = size if isinstance(size, tuple) else (size, size)
        NC = 127 if w * h >= 4096 else 63
        im = im.resize((w, h), Image.LANCZOS)
        rgb = im.convert("RGB").quantize(colors=NC, method=Image.MEDIANCUT, dither=Image.NONE)
        pal = rgb.getpalette()[:NC*3]
        idx = np.array(rgb, dtype=np.uint8) + 1  # index 0 reserved
        alpha = np.array(im)[:, :, 3]
        cut = alpha < 128
        has_cut = allow_cut and bool(cut.any()) and (a < 128).mean() > 0.02
        idx[cut & has_cut] = 0
        pal15 = [0] + [((pal[i*3]>>3)|((pal[i*3+1]>>3)<<5)|((pal[i*3+2]>>3)<<10)) for i in range(len(pal)//3)]
        pal15 += [0]*(NC+1-len(pal15))
        r = dict(w=w, h=h, pal=pal15, data=idx.tobytes(), cut=has_cut, name=os.path.basename(p))
    except Exception as e:
        print("tex fail", p, e)
    _tex[(guid, size, allow_cut)] = r
    return r

def obbify(tp, uv, minlen, ratio=3.0):
    """Replace long thin connected components (beams, planks, rails) by the 4 side faces of their oriented
    box, as quads. Returns (rest_tp, rest_uv, quads (m,4,3), quad_uv (m,4,2))."""
    from scipy.sparse.csgraph import connected_components
    from scipy.sparse import coo_matrix
    q = np.round(tp.reshape(-1, 3) * 1000).astype(np.int64)
    _, inv = np.unique(q, axis=0, return_inverse=True); tri = inv.reshape(-1, 3); nv = tri.max() + 1
    r = np.concatenate([tri[:, 0], tri[:, 1]]); c = np.concatenate([tri[:, 1], tri[:, 2]])
    nc, lab = connected_components(coo_matrix((np.ones(len(r)), (r, c)), shape=(nv, nv)), directed=False)
    tl = lab[tri[:, 0]]
    keep = np.ones(len(tp), bool); quads = []; quv = []
    for i in range(nc):
        m = tl == i
        p = tp[m].reshape(-1, 3); mu = p.mean(0)
        if len(p) < 12: continue
        w, V = np.linalg.eigh(np.cov((p - mu).T))
        V = V[:, ::-1]                              # V[:,0] = long axis
        pr = (p - mu) @ V; lo_, hi_ = pr.min(0), pr.max(0); e = hi_ - lo_
        if e[0] < minlen or e[0] < ratio * e[1]: continue
        keep[m] = False
        a0, a1, a2 = V[:, 0], V[:, 1], V[:, 2]
        def P(t, u, v): return mu + a0 * t + a1 * u + a2 * v
        t0, t1 = lo_[0], hi_[0]
        # 4 sides around the long axis; each face: corners in order giving outward normal with cross(b-a,c-a)
        sides = ((hi_[1], None, 0, 1), (lo_[1], None, 0, -1), (None, hi_[2], 1, 0), (None, lo_[2], -1, 0))
        if e[2] < 0.3 * e[1]: sides = sides[2:]     # flat plank: only its two broad faces
        for (u, v, du, dv) in sides:
            if v is None:   # face normal +-a1, spans a0 x a2
                n = a1 * dv; c0 = [P(t0, u, lo_[2]), P(t1, u, lo_[2]), P(t1, u, hi_[2]), P(t0, u, hi_[2])]; wdt = e[2]
            else:
                n = a2 * du; c0 = [P(t0, lo_[1], v), P(t1, lo_[1], v), P(t1, hi_[1], v), P(t0, hi_[1], v)]; wdt = e[1]
            c0 = np.array(c0)
            if np.dot(np.cross(c0[1] - c0[0], c0[2] - c0[0]), n) < 0: c0 = c0[::-1]
            quads.append(c0)
            L = np.linalg.norm(c0[1] - c0[0]); W = np.linalg.norm(c0[3] - c0[0])
            quv.append([[0, 0], [L, 0], [L, W], [0, W]])
    return tp[keep], uv[keep], np.array(quads).reshape(-1, 4, 3), np.array(quv, float).reshape(-1, 4, 2)

def cablify(tp, nseg=8, width=0.05):
    """sagging cable/wire mesh -> nseg thin double-sided strips along its centre line: quads (m,4,3), uv (m,4,2)"""
    p = tp.reshape(-1, 3); mu = p.mean(0)
    w, V = np.linalg.eigh(np.cov((p - mu).T)); a0 = V[:, -1]
    t = (p - mu) @ a0; edges = np.linspace(t.min(), t.max(), nseg + 1)
    ctr = []
    for i in range(nseg + 1):
        m = np.abs(t - edges[i]) <= (edges[1] - edges[0]) * 0.5
        ctr.append(p[m].mean(0) if m.any() else mu + a0 * edges[i])
    ctr = np.array(ctr); quads = []
    side = np.cross(a0, [0, 1, 0]); side = side / (np.linalg.norm(side) + 1e-9) * width / 2
    up = np.cross(side, a0); up = up / (np.linalg.norm(up) + 1e-9) * width / 2
    for i in range(nseg):
        a, b = ctr[i], ctr[i + 1]
        for d in (side, up):
            q = np.array([a - d, b - d, b + d, a + d]); quads += [q, q[::-1]]
    quads = np.array(quads)
    return quads, np.zeros((len(quads), 4, 2))

def alpha31(mat):
    """31 = opaque, else DS polygon alpha for transparent (fade/transparent/URP surface) materials"""
    if not mat: return 31
    f = mat["floats"]
    tr = f.get("_Surface", 0) == 1 or f.get("_Mode", 0) in (2, 3) or mat["queue"] >= 2900 or "_ALPHAPREMULTIPLY_ON" in mat["kw"] or "_SURFACE_TYPE_TRANSPARENT" in mat["kw"]
    if not tr: return 31
    return int(min(12, max(4, round(mat["col"][3] * 16))))

def cutout(mat):
    """alpha-tested material (texture alpha = holes) as opposed to alpha used as smoothness/mask"""
    if not mat: return False
    f = mat["floats"]
    return (f.get("_AlphaClip", 0) == 1 or f.get("_Mode", 0) == 1 or "_ALPHATEST_ON" in mat["kw"]
            or 2450 <= mat["queue"] < 2900 or f.get("_Cutoff", 0) > 0 and f.get("_AlphaClip", 1) != 0 and mat["queue"] >= 2450)

NAMES = []   # GameObject name of each collected item (debug / filters)
def collect(scene, lo, hi):
    """All active renderer triangles with centroid inside [lo,hi]: list of (key, tris (n,3,3) world, uv (n,3,2))."""
    meshc = {}
    nrend = 0
    items = []
    only = [x for x in os.environ.get("ONLY", "").split(",") if x]; skip = [x for x in os.environ.get("SKIP", "").split(",") if x]
    for gf, g in scene.go.items():
        if "tf" not in g or not scene.active_in_hierarchy(gf): continue
        if only or skip:
            pth = scene.path(gf)
            if only and not any(o in pth for o in only): continue
            if any(o in pth for o in skip): continue
        mf = scene.comps(gf, 33); mr = scene.comps(gf, 23)
        if not mf or not mr: continue
        r = mr[0][2]
        if not r.get("m_Enabled", 1): continue
        mg = mf[0][2]["m_Mesh"].get("guid")
        if mg not in G: continue
        if mg not in meshc:
            try: meshc[mg] = meshio.load(os.path.join(unity.ROOT, G[mg]))
            except Exception: meshc[mg] = None
        m = meshc[mg]
        if m is None or m.pos is None: continue
        p, q, s = scene.world(g["tf"])
        batched = r.get("m_StaticBatchInfo", {}).get("subMeshCount", 0) > 0
        if batched:
            sm = range(r["m_StaticBatchInfo"]["firstSubMesh"], r["m_StaticBatchInfo"]["firstSubMesh"] + r["m_StaticBatchInfo"]["subMeshCount"])
            wp = m.pos.astype(np.float64)
        else:
            sm = range(len(m.subs))
            wp = (m.pos.astype(np.float64) * np.array(s)) @ quat_mat(q).T + np.array(p)
            wp[:, 0] *= 1  # unity left-handed -> keep
        mats = r.get("m_Materials", [])
        used = False
        for k, si in enumerate(sm):
            if si >= len(m.subs) or len(m.subs[si]) == 0: continue
            mi = k if batched else si
            mat = matio.load(mats[mi]["guid"]) if mi < len(mats) and mats[mi].get("guid") else None
            tg = mat["tex"] if mat else None
            col = mat["col"] if mat else (1, 1, 1, 1)
            tile = mat["tile"] if mat else (1, 1); off = mat["off"] if mat else (0, 0)
            t = m.subs[si]
            tp = wp[t]                      # (n,3,3)
            c = tp.mean(axis=1)
            keep = np.all((c >= lo) & (c <= hi), axis=1)
            if not keep.any(): continue
            uv = m.uv[t] if m.uv is not None else np.zeros((len(t), 3, 2))
            uv = uv * np.array(tile) + np.array(off)
            key = (tg, tuple(np.round(col[:3], 2)), alpha31(mat), cutout(mat))
            items.append((key, tp[keep], uv[keep])); used = True; NAMES.append(g["name"])
        nrend += used
    return items, nrend

def main():
    scn, x0, x1, y0, y1, z0, z1, out = sys.argv[1:9]
    tsize = int(sys.argv[9]) if len(sys.argv) > 9 else 64
    lo = np.array([float(x0), float(y0), float(z0)]); hi = np.array([float(x1), float(y1), float(z1)])
    ctr = (lo + hi) / 2
    if os.environ.get("PIVOT"): ctr = np.array([float(v) for v in os.environ["PIVOT"].split(",")])
    scene = unity.Scene(scn)
    buckets = collections.defaultdict(lambda: ([], [], [], []))  # key -> (tri lists of (3,3) pos, uv (3,2))
    items, nrend = collect(scene, lo, hi)
    excl = os.environ.get("EXCL")
    if excl:
        e = [float(v) for v in excl.split(",")]; elo = np.array(e[0::2]); ehi = np.array(e[1::2])
        items2 = []
        for key, tpk, uvk in items:
            c = tpk.mean(axis=1); ok = ~np.all((c >= elo) & (c <= ehi), axis=1)
            if ok.any(): items2.append((key, tpk[ok], uvk[ok]))
        items = items2
    if os.environ.get("TERRAIN"):
        # ground decals (roads, paths) lying on the terrain: the terrain splat already paints them, drop them
        import terrain; nd = 0; items2 = []
        for key, tpk, uvk in items:
            on = np.all(np.abs(tpk[:, :, 1] - terrain.height(tpk[:, :, 0], tpk[:, :, 2])) < 0.3, axis=1)
            nd += on.sum()
            if (~on).any(): items2.append((key, tpk[~on], uvk[~on]))
        items = items2; print("dropped ground decal tris", nd)
    items2 = []
    for key, tpk, uvk in items:
        p = tpk.reshape(-1, 3); e = np.sort(np.linalg.eigvalsh(np.cov(p.T))) if len(tpk) >= 20 else None
        if e is not None and e[2] > 25 and e[0] < 0.1 and e[2] > 20 * e[1]:    # long (>~15 m), thin, only sagging: a cable
            qp, qu = cablify(tpk); buckets[key][2].append(qp); buckets[key][3].append(qu); print("cable", len(tpk), "->", len(qp))
        else: items2.append((key, tpk, uvk))
    items = items2
    BUDGET = int(os.environ.get("BUDGET", 3500))
    # share the budget by sqrt(tri count) weighted by physical size (big structures keep their shape)
    SIZEW = float(os.environ.get("SIZEW", 0))
    ns = np.array([len(i[1]) for i in items], dtype=np.float64)
    diag = np.array([np.linalg.norm(t.reshape(-1, 3).max(0) - t.reshape(-1, 3).min(0)) for _, t, _ in items])
    wt = np.sqrt(ns) * np.maximum(diag, 0.05) ** SIZEW
    k = BUDGET / wt.sum()
    OBB = float(os.environ.get("OBB", 0))
    if OBB:
        items3 = []; nq = 0
        for key, tpk, uvk in items:
            if len(tpk) >= 24:
                tpk, uvk, qp, qu = obbify(tpk, uvk, OBB)
                if len(qp): buckets[key][2].append(qp); buckets[key][3].append(qu); nq += len(qp)
            if len(tpk): items3.append((key, tpk, uvk))
        items = items3; print("obb quads", nq)
        ns = np.array([len(i[1]) for i in items], dtype=np.float64)
        diag = np.array([np.linalg.norm(t.reshape(-1, 3).max(0) - t.reshape(-1, 3).min(0)) for _, t, _ in items])
        wt = np.sqrt(ns) * np.maximum(diag, 0.05) ** SIZEW
        k = BUDGET / wt.sum()
    for (key, tpk, uvk), w in zip(items, wt):
        tgt = int(w * k)
        if tgt < 4 and SIZEW: continue          # too small to matter at this budget
        tgt = max(6, tgt)
        a, b = decimate(tpk, uvk, tgt)
        if len(a): buckets[key][0].append(a); buckets[key][1].append(b)
    if os.environ.get("TERRAIN"):
        import terrain
        for tg, t, uv in terrain.terrain_tris(lo[0], hi[0], lo[2], hi[2], float(os.environ["TERRAIN"])):
            key = (tg, (1.0, 1.0, 1.0), 31, False); buckets[key][0].append(t); buckets[key][1].append(uv)
    print("renderers used", nrend, "items", len(items), "buckets", len(buckets))
    os.makedirs(out, exist_ok=True)
    tex_list = []; blob = bytearray(); groups = []
    total = 0
    for key, (tps, uvs, qps, qus) in sorted(buckets.items(), key=lambda kv: (kv[0][2] < 31, -sum(len(a) for a in kv[1][0] + kv[1][2]))):
        tp = np.concatenate(tps) if tps else np.zeros((0, 3, 3)); uv = np.concatenate(uvs) if uvs else np.zeros((0, 3, 2))
        qp = np.concatenate(qps) if qps else np.zeros((0, 4, 3)); qu = np.concatenate(qus) if qus else np.zeros((0, 4, 2))
        qp = qp - ctr; qp[:, :, 2] *= -1; qu = qu.copy(); qu[:, :, 1] = 1 - qu[:, :, 1]
        tp = tp - ctr; tp[:, :, 2] *= -1          # DS space
        # flipping z reverses winding relative to unity; unity is CW front, flip restores CCW
        tp = tp.astype(np.float64)
        uv = uv.copy(); uv[:, :, 1] = 1 - uv[:, :, 1]
        tex = conv_tex(key[0], tsize if len(tp) + len(qp) > 150 else max(16, tsize // 2), key[2] == 31 and key[3]) if key[0] else None
        # degenerate removal
        e1 = tp[:, 1] - tp[:, 0]; e2 = tp[:, 2] - tp[:, 0]
        area = np.linalg.norm(np.cross(e1, e2), axis=1)
        ok = area > 1e-6
        tp, uv = tp[ok], uv[ok]
        total += len(tp) + len(qp)
        groups.append((key, tex, tp, uv, qp, qu))
    print("cutout:", sorted({g[1]["name"] for g in groups if g[1] and g[1]["cut"]}))
    print("total tris", total, "textures", len([g for g in groups if g[1]]))
    for key, tex, tp, uv, qp, qu in groups[:20]:
        print(len(tp), len(qp), key[0][:8] if key[0] else None, tex["name"] if tex else "-")
    emit(groups, out, ctr)

def emit(groups, out, ctr):
    """level.bin: u32 magic,'LVL0'; u32 n; per group header 8 u32: w,h,col15,flags,dlwords,paloff,texoff,dloff; then blob."""
    hdr = []; blob = bytearray(); ngr = 0
    for key, tex, tp, uv, qp, qu in groups:
        if len(tp) + len(qp) == 0: continue
        col = key[1]
        c15 = gx.pack_color15(*[min(1, max(0, x)) for x in col])
        w = h = 0; paloff = texoff = 0; flags = 0
        if tex:
            w, h = tex["w"], tex["h"]; flags = 1 | (2 if tex["cut"] else 0) | (len(tex["pal"]) << 8)
            paloff = len(blob); blob += struct.pack("<%dH" % len(tex["pal"]), *tex["pal"])
            texoff = len(blob); blob += tex["data"]
        tp = np.clip(tp / SCALE, -7.99, 7.99)
        flags |= key[2] << 24
        pos = tp.reshape(-1, 3); uvs = uv.reshape(-1, 2)
        tris = np.arange(len(pos)).reshape(-1, 3)[:, ::-1]
        cmds = gx.triangles(pos, None, uvs if tex else None, tris, (w or 1, h or 1)) if len(tp) else []
        if len(qp):
            qpos = np.clip(qp / SCALE, -7.99, 7.99).reshape(-1, 3)
            quads = np.arange(len(qpos)).reshape(-1, 4)[:, ::-1]
            cmds += gx.quads(qpos, qu.reshape(-1, 2) if tex else None, quads, (w or 1, h or 1))
        dl = gx.encode(cmds)
        while len(blob) % 4: blob += b"\0"
        dloff = len(blob); blob += dl
        hdr.append(struct.pack("<8I", w, h, c15, flags, len(dl)//4 - 1, paloff, texoff, dloff))
        ngr += 1
    base = 24 + 32 * ngr
    hdr2 = b"".join(struct.pack("<8I", *(list(struct.unpack("<8I", hh)[:5]) + [struct.unpack("<8I", hh)[5]+base if struct.unpack("<8I", hh)[3]&1 else 0, struct.unpack("<8I", hh)[6]+base if struct.unpack("<8I", hh)[3]&1 else 0, struct.unpack("<8I", hh)[7]+base])) for hh in hdr)
    open(os.path.join(out, "level.bin"), "wb").write(b"LVL1" + struct.pack("<I", ngr) + struct.pack("<Iiii", int(SCALE * 4096), int(round(ctr[0] * 4096)), int(round(ctr[1] * 4096)), int(round(-ctr[2] * 4096))) + hdr2 + bytes(blob))
    print("level.bin", 24 + 32*ngr + len(blob), "bytes,", ngr, "groups")

if __name__ == "__main__":
    main()
