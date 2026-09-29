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

def quat_mat(q):
    x, y, z, w = q
    return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                     [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                     [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])

def texfile(guid):
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
def conv_tex(guid, size):
    """returns (index-key, w, h, pal(list of 15-bit), data bytes 8bpp) or None"""
    if guid in _tex: return _tex[guid]
    p = texfile(guid); r = None
    try:
        im = Image.open(p).convert("RGBA")
        a = np.array(im)[:, :, 3]
        w = h = size
        im = im.resize((w, h), Image.LANCZOS)
        rgb = im.convert("RGB").quantize(colors=255, method=Image.MEDIANCUT, dither=Image.NONE)
        pal = rgb.getpalette()[:255*3]
        idx = np.array(rgb, dtype=np.uint8) + 1  # index 0 reserved
        alpha = np.array(im)[:, :, 3]
        cut = alpha < 128
        has_cut = bool(cut.any()) and (a < 128).mean() > 0.02
        idx[cut & has_cut] = 0
        pal15 = [0] + [((pal[i*3]>>3)|((pal[i*3+1]>>3)<<5)|((pal[i*3+2]>>3)<<10)) for i in range(len(pal)//3)]
        pal15 += [0]*(256-len(pal15))
        r = dict(w=w, h=h, pal=pal15, data=idx.tobytes(), cut=has_cut, name=os.path.basename(p))
    except Exception as e:
        print("tex fail", p, e)
    _tex[guid] = r
    return r

def main():
    scn, x0, x1, y0, y1, z0, z1, out = sys.argv[1:9]
    tsize = int(sys.argv[9]) if len(sys.argv) > 9 else 64
    lo = np.array([float(x0), float(y0), float(z0)]); hi = np.array([float(x1), float(y1), float(z1)])
    ctr = (lo + hi) / 2
    scene = unity.Scene(scn)
    meshc = {}
    buckets = collections.defaultdict(lambda: ([], []))  # key -> (tri lists of (3,3) pos, uv (3,2))
    nrend = 0
    items = []
    for gf, g in scene.go.items():
        if "tf" not in g or not scene.active_in_hierarchy(gf): continue
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
            key = (tg, tuple(np.round(col[:3], 2)))
            items.append((key, tp[keep], uv[keep])); used = True
        nrend += used
    BUDGET = int(os.environ.get("BUDGET", 3500))
    ns = np.array([len(i[1]) for i in items], dtype=np.float64)
    k = BUDGET / np.sqrt(ns).sum()
    for key, tpk, uvk in items:
        tgt = max(6, int(np.sqrt(len(tpk)) * k))
        a, b = decimate(tpk, uvk, tgt)
        if len(a): buckets[key][0].append(a); buckets[key][1].append(b)
    print("renderers used", nrend, "items", len(items), "buckets", len(buckets))
    os.makedirs(out, exist_ok=True)
    tex_list = []; blob = bytearray(); groups = []
    total = 0
    for key, (tps, uvs) in sorted(buckets.items(), key=lambda kv: -sum(len(a) for a in kv[1][0])):
        tp = np.concatenate(tps); uv = np.concatenate(uvs)
        tp = tp - ctr; tp[:, :, 2] *= -1          # DS space
        # flipping z reverses winding relative to unity; unity is CW front, flip restores CCW
        tp = tp.astype(np.float64)
        uv = uv.copy(); uv[:, :, 1] = 1 - uv[:, :, 1]
        tex = conv_tex(key[0], tsize if len(tp) > 150 else max(16, tsize // 2)) if key[0] else None
        # degenerate removal
        e1 = tp[:, 1] - tp[:, 0]; e2 = tp[:, 2] - tp[:, 0]
        area = np.linalg.norm(np.cross(e1, e2), axis=1)
        ok = area > 1e-6
        tp, uv = tp[ok], uv[ok]
        total += len(tp)
        groups.append((key, tex, tp, uv))
    print("total tris", total, "textures", len([g for g in groups if g[1]]))
    for key, tex, tp, uv in groups[:20]:
        print(len(tp), key[0][:8] if key[0] else None, tex["name"] if tex else "-")
    emit(groups, out)

def emit(groups, out):
    """level.bin: u32 magic,'LVL0'; u32 n; per group header 8 u32: w,h,col15,flags,dlwords,paloff,texoff,dloff; then blob."""
    hdr = []; blob = bytearray(); ngr = 0
    for key, tex, tp, uv in groups:
        if len(tp) == 0: continue
        col = key[1]
        c15 = gx.pack_color15(*[min(1, max(0, x)) for x in col])
        w = h = 0; paloff = texoff = 0; flags = 0
        if tex:
            w, h = tex["w"], tex["h"]; flags = 1 | (2 if tex["cut"] else 0)
            paloff = len(blob); blob += struct.pack("<256H", *tex["pal"])
            texoff = len(blob); blob += tex["data"]
        tp = np.clip(tp, -7.99, 7.99)
        pos = tp.reshape(-1, 3); uvs = uv.reshape(-1, 2)
        tris = np.arange(len(pos)).reshape(-1, 3)[:, ::-1]
        cmds = gx.triangles(pos, None, uvs if tex else None, tris, (w or 1, h or 1))
        dl = gx.encode(cmds)
        while len(blob) % 4: blob += b"\0"
        dloff = len(blob); blob += dl
        hdr.append(struct.pack("<8I", w, h, c15, flags, len(dl)//4 - 1, paloff, texoff, dloff))
        ngr += 1
    base = 8 + 32 * ngr
    hdr2 = b"".join(struct.pack("<8I", *(list(struct.unpack("<8I", hh)[:5]) + [struct.unpack("<8I", hh)[5]+base if struct.unpack("<8I", hh)[3]&1 else 0, struct.unpack("<8I", hh)[6]+base if struct.unpack("<8I", hh)[3]&1 else 0, struct.unpack("<8I", hh)[7]+base])) for hh in hdr)
    open(os.path.join(out, "level.bin"), "wb").write(b"LVL0" + struct.pack("<I", ngr) + hdr2 + bytes(blob))
    print("level.bin", 8 + 32*ngr + len(blob), "bytes,", ngr, "groups")

if __name__ == "__main__":
    main()
