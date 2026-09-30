#!/usr/bin/env python3
"""bake_world.py NAME out.wld  -- bake a large outdoor area (drive route, trailhead, campsite...) into a chunked world.

World definitions live in WORLDS below. Frame: DS world = (ux, uy, -uz) metres, 20.12 fixed.

WLD1 layout (all little endian, offsets from file start):
  'WLD1', u32 ntex, nchunk, nbox, s32 csize, gx0, gz0, u32 gnx, gnz, grid_off, tex_off, chunk_off, box_off   (13 words)
  grid:   u16[gnx*gnz] chunk index per cell (0xFFFF none), cell (i,j) covers x in gx0+i*csize.., z in gz0+j*csize..
  tex:    ntex x { u32 w, h, npal | cut<<16, paloff, texoff }
  chunk:  nchunk x { s32 cx, cy, cz, scale, u32 ngroups, grpoff, flooroff, radius }
  group:  { s32 tex, u32 col15 | alpha<<16, u32 dlwords, dloff }
  floor:  per chunk 16 x 16 s16 heights (1/32 m) at 2 m cell centres; -32768 = none
  box:    nbox x { s32 x0, x1, y0, y1, z0, z1 }
"""
import sys, os, struct, collections, re, itertools
import numpy as np
import unity, meshio, matio, gx
from bake_level import collect, decimate, conv_tex, alpha31, cutout, quat_mat
G = unity.guid_map()

WORLDS = {
    # First Scene: RV start -> Roseburg diner (Roseburg is enabled at runtime by OptimizationFirstScene)
    "drive1": dict(scene="First Scene", terrain="TerrainData/New Terrain 1.asset", tofs=(0, 0, 0),
                   start=(608, 270), goal=(1116, 963), extra=[(900, 1200, 850, 1100)],
                   force=["Roseburg (DISABLE ON BUILD)", "Batch1", "Batch2", "Batch3", "Batch4"],
                   skip=["UI Manager", "OLD", "Traffic Manager", "Follower (Truck)", "RV/", "INV colliders", "Fell Off Map",
                         "FirstPersonController", "Triggers"],
                   road="Road Network"),
    # Trail Start: RV start -> Ironbark trailhead guard house (+ the trail past the gate)
    "trail": dict(scene="Trail Start", terrain="TerrainData/New Terrain 2.asset", tofs=(179.5, 0, 25.1),
                  start=(584, 153), goal=(622, 420), extra=[(600, 700, 360, 470)],
                  force=[],
                  skip=["UI Manager", "Old Scene Stuff", "Game Manager", "RV/", "INV Colliders", "FirstPersonController",
                        "Triggers", "GuardHouse", "TRAIL GATE", "Particles"],
                  road="Road Network",
                  boxskip=["RV$", "RV/", "INV Colliders", "FirstPersonController", "UI Manager", "Triggers", "TRAIL GATE", "GuardHouse/Invisible", "GuardHouse/Door"]),
    # Camping (Lacey Trail campsite): trail from the tower side -> campfire -> fenced area
    "camp": dict(scene="Camping", terrain="TerrainData/LaceyTrailCampsite.asset", tofs=(0, 0, 0),
                 start=(40, 45), goal=(92, 101), extra=[(20, 125, 25, 130)],
                 force=[],
                 skip=["UIManager", "UI", "Game Manager", "FirstPersonController", "Trigger/", "Cultist", "Water Pot",
                       "Particles", "Invisible Colliders", "Screenshot", "Lake", "nav"],
                 road=None,
                 boxskip=["FirstPersonController", "Trigger/", "Cultist", "Water Pot", "UIManager", "Game Manager"]),
}

CS = 32.0            # chunk size (m)
FC = 2.0             # floor cell (m)
FN = int(CS / FC)
TPC = int(os.environ.get("TPC", 170))    # average triangle budget per chunk (visible budget ~ 10 chunks)
TSTEP = float(os.environ.get("TSTEP", 8)) # terrain grid step (m)

def main():
    name, out = sys.argv[1], sys.argv[2]
    W = WORLDS[name]
    os.environ["TERRAIN_ASSET"] = W["terrain"]; os.environ["TERRAIN_OFS"] = ",".join(str(v) for v in W["tofs"])
    import terrain
    s = unity.Scene(W["scene"])
    for f, g in s.go.items():
        if g["name"] in W["force"]: g["active"] = 1
    os.environ["SKIP"] = ",".join(W["skip"])
    import bake_level
    # everything in the route bbox
    xs = [W["start"][0], W["goal"][0]] + [e[0] for e in W["extra"]] + [e[1] for e in W["extra"]]
    zs = [W["start"][1], W["goal"][1]] + [e[2] for e in W["extra"]] + [e[3] for e in W["extra"]]
    lo = np.array([min(xs) - 200, -100, min(zs) - 200]); hi = np.array([max(xs) + 200, 400, max(zs) + 200])
    bake_level.NAMES.clear()
    items, nrend = collect(s, lo, hi)
    names = list(bake_level.NAMES)
    print("renderers", nrend, "items", len(items))
    # ---- chunk selection: road cells, BFS path, dilate
    R = 16.0
    road = set()
    # road renderers: those under the road root; recollect with ONLY
    ritems = []
    if W.get("road"):
        os.environ["ONLY"] = W["road"]; os.environ.pop("SKIP")
        bake_level.NAMES.clear(); ritems, _ = collect(s, lo, hi); os.environ.pop("ONLY"); os.environ["SKIP"] = ",".join(W["skip"])
    else:   # walking areas: every cell of the extra boxes is "road"
        for (x0, x1, z0, z1) in W["extra"]:
            for x in range(int(x0 // R), int(x1 // R) + 1):
                for z in range(int(z0 // R), int(z1 // R) + 1): road.add((x, z))
    for key, tp, uv in ritems:
        c = tp.mean(axis=1)
        for x, z in np.unique(np.floor(c[:, [0, 2]] / R).astype(int), axis=0): road.add((int(x), int(z)))
    st = (int(W["start"][0] // R), int(W["start"][1] // R)); gl = (int(W["goal"][0] // R), int(W["goal"][1] // R))
    road.add(st); road.add(gl)
    prev = {st: None}; q = collections.deque([st])
    while q:
        c = q.popleft()
        if c == gl: break
        for dx, dz in itertools.product((-1, 0, 1), repeat=2):
            n = (c[0] + dx, c[1] + dz)
            if n in road and n not in prev: prev[n] = c; q.append(n)
    assert gl in prev, "no road path"
    path = []; c = gl
    while c: path.append(c); c = prev[c]
    print("route cells", len(path), "(%.0f m)" % (len(path) * R))
    chunks = set()
    D = float(os.environ.get("DILATE", 40))
    for (x, z) in path:
        cx, cz = (x + 0.5) * R, (z + 0.5) * R
        for i in range(int((cx - D) // CS), int((cx + D) // CS) + 1):
            for j in range(int((cz - D) // CS), int((cz + D) // CS) + 1): chunks.add((i, j))
    for (x0, x1, z0, z1) in W["extra"]:
        for i in range(int(x0 // CS), int(x1 // CS) + 1):
            for j in range(int(z0 // CS), int(z1 // CS) + 1): chunks.add((i, j))
    chunks = sorted(chunks)
    cidx = {c: k for k, c in enumerate(chunks)}
    print("chunks", len(chunks))
    def chunk_of(p):   # p (n,2) unity x,z -> chunk keys
        return [ (int(a), int(b)) for a, b in np.floor(p / CS).astype(int)]
    # ---- per-renderer decimation, budget by chunk count
    keep_items = []
    for key, tp, uv in items:
        c = tp.mean(axis=1)
        m = np.array([k in cidx for k in chunk_of(c[:, [0, 2]])])
        if m.any(): keep_items.append((key, tp[m], uv[m]))
    items = keep_items
    ns = np.array([len(t) for _, t, _ in items], float)
    wt = np.sqrt(ns)
    budget = TPC * len(chunks) * 0.55
    k = budget / wt.sum()
    buckets = collections.defaultdict(list)   # (chunk, key) -> [(tp, uv)]
    ntri = 0
    for (key, tp, uv), w in zip(items, wt):
        a, b = decimate(tp, uv, max(4, int(w * k)))
        if not len(a): continue
        for ck, t1, u1 in split_chunks(a, b, cidx):
            buckets[(ck, key)].append((t1, u1)); ntri += len(t1)
    print("mesh tris", ntri)
    # ---- terrain: grid per chunk, lowered under the road
    roadh = {}
    for key, tp, uv in ritems:
        for t in tp:
            for v in t: roadh[(int(v[0] // 2), int(v[2] // 2))] = min(roadh.get((int(v[0] // 2), int(v[2] // 2)), 1e9), v[1])
    nt = 0
    for (i, j) in chunks:
        for tg, t, uv in terrain.terrain_tris(i * CS, (i + 1) * CS, j * CS, (j + 1) * CS, TSTEP):
            t = t.copy()
            for v in t.reshape(-1, 3):
                rh = min([roadh.get((int(v[0] // 2) + a, int(v[2] // 2) + b), 1e9) for a in (-2, -1, 0, 1, 2) for b in (-2, -1, 0, 1, 2)])
                if rh < 1e8: v[1] = min(v[1], rh - 0.35)
            key = (tg, (1.0, 1.0, 1.0), 31, False)
            buckets[((i, j), key)].append((t, uv)); nt += len(t)
    print("terrain tris", nt)
    # ---- trees: lowest LOD of each prototype
    protos = {}
    ntree = 0
    for pf, p, rot, ws, hs in terrain.trees():
        ck = (int(p[0] // CS), int(p[2] // CS))
        if ck not in cidx: continue
        if pf not in protos: protos[pf] = tree_lod(pf)
        pr = protos[pf]
        if pr is None: continue
        tkey, ttp, tuv = pr
        c, s_ = np.cos(rot), np.sin(rot)
        # unity rotates tree around Y by 'rotation' radians
        Rm = np.array([[c, 0, s_], [0, 1, 0], [-s_, 0, c]])
        wp = (ttp.reshape(-1, 3) * np.array([ws, hs, ws])) @ Rm.T + p
        buckets[(ck, tkey)].append((wp.reshape(-1, 3, 3), tuv)); ntree += 1
    print("trees", ntree, "protos", len(protos))
    # ---- cap each chunk's triangle count (dense town blocks)
    CAP = int(os.environ.get("CAP", 650))
    per = collections.defaultdict(list)
    for (ck, key), lst in buckets.items(): per[ck].append(key)
    for ck, keys in per.items():
        n = sum(len(t) for key in keys for t, _ in buckets[(ck, key)])
        if n <= CAP: continue
        f = CAP / n
        for key in keys:
            if key[0] and key[0].startswith("/"): continue     # impostors are already 4 tris
            tp = np.concatenate([t for t, _ in buckets[(ck, key)]]); uv = np.concatenate([u for _, u in buckets[(ck, key)]])
            a, b = decimate(tp, uv, max(2, int(len(tp) * f)))
            buckets[(ck, key)] = [(a, b)] if len(a) else []
        print("chunk", ck, n, "->", sum(len(t) for key in keys for t, _ in buckets[(ck, key)]))
    for k_ in [k_ for k_, v in buckets.items() if not v]: del buckets[k_]
    # ---- textures (shared)
    tex_use = collections.Counter()
    for (ck, key), lst in buckets.items():
        if key[0]: tex_use[(key[0], key[2] == 31 and key[3])] += sum(len(t) for t, _ in lst)
    order = [t for t, _ in tex_use.most_common()]
    sizes = {t: ((32, 64) if t[0].startswith("/") else 64 if i < int(os.environ.get("N64", 40)) else 32) for i, t in enumerate(order)}
    texs = []; tindex = {}
    for t in order:
        r = conv_tex(t[0], sizes[t], t[1])
        if r is None: continue
        tindex[t] = len(texs); texs.append(r)
    vram = sum(r["w"] * r["h"] for r in texs)
    print("textures", len(texs), "vram %d KB" % (vram // 1024))
    # ---- colliders
    os.environ["BOXSKIP"] = ",".join(W.get("boxskip", []))
    boxes = collide_boxes(s, lo, hi, cidx)
    # ---- floor heights: 1 m cells, max over upward faces (terrain + meshes) per chunk
    floors = {}
    for ck in chunks:
        floors[ck] = np.full((FN, FN), -1e9)
    for (ck, key), lst in buckets.items():
        if key[0] and key[0].startswith("/"): continue
        for t, _ in lst: raster_floor(floors[ck], ck, t)
    for (i, j) in chunks:   # fill holes with true terrain
        f = floors[(i, j)]
        xs_ = i * CS + (np.arange(FN) + 0.5) * FC; zs_ = j * CS + (np.arange(FN) + 0.5) * FC
        X, Z = np.meshgrid(xs_, zs_)
        th = terrain.height(X, Z)
        floors[(i, j)] = np.where(f < -1e8, th, np.maximum(f, th - 5))
    emit(out, chunks, cidx, buckets, texs, tindex, boxes, floors)

def split_chunks(tp, uv, cidx):
    c = tp.mean(axis=1)
    ck = np.floor(c[:, [0, 2]] / CS).astype(int)
    out = []
    for u in np.unique(ck, axis=0):
        k = (int(u[0]), int(u[1]))
        if k not in cidx: continue
        m = np.all(ck == u, axis=1)
        out.append((k, tp[m], uv[m]))
    return out

def tree_lod(pf):
    """impostor: the lowest LOD's front plane cropped out of the atlas -> 2 crossed quads (4 tris)"""
    from PIL import Image
    t = open(os.path.join(unity.ROOT, pf)).read()
    ms = re.findall(r"m_Mesh: \{fileID: \d+, guid: ([0-9a-f]+)", t)
    mats = re.findall(r"m_Materials:\n((?:  - .*\n)+)", t)
    if not ms: return None
    m = meshio.load(os.path.join(unity.ROOT, G[ms[-1]]))
    mat = matio.load(re.findall(r"guid: ([0-9a-f]+)", mats[-1])[0])
    if not mat or not mat["tex"]: return None
    tri = np.concatenate(m.subs); P = m.pos[tri].astype(np.float64).reshape(-1, 3); U = m.uv[tri].astype(np.float64).reshape(-1, 2)
    sel = (np.abs(P[:, 2]) < 0.35) & (U[:, 0] < 0.5)
    if sel.sum() < 6: sel = np.abs(P[:, 2]) < 0.35
    p, u = P[sel], U[sel]
    au, bu = np.polyfit(p[:, 0], u[:, 0], 1); av, bv = np.polyfit(p[:, 1], u[:, 1], 1)
    x0, x1 = p[:, 0].min(), p[:, 0].max(); y0, y1 = P[:, 1].min(), P[:, 1].max()
    us = sorted([au * x0 + bu, au * x1 + bu]); vs = sorted([av * y0 + bv, av * y1 + bv])
    im = Image.open(os.path.join(unity.ROOT, G[mat["tex"]])).convert("RGBA")
    W, H = im.size
    crop = im.crop((int(us[0] * W), int((1 - vs[1]) * H), int(np.ceil(us[1] * W)), int(np.ceil((1 - vs[0]) * H))))
    fn = os.path.join(unity.CACHE, "tree_" + os.path.basename(pf).replace(".prefab", ".png")); crop.save(fn)
    r = (x1 - x0) / 2
    # texture u runs with x if au > 0
    ua, ub = (0.0, 1.0) if au > 0 else (1.0, 0.0)
    tp = []; uv = []
    for ax in ((1, 0, 0), (0, 0, 1)):
        ax = np.array(ax, float)
        a_ = -r * ax + [0, y0, 0]; b_ = r * ax + [0, y0, 0]; c_ = r * ax + [0, y1, 0]; d_ = -r * ax + [0, y1, 0]
        tp += [[a_, b_, c_], [a_, c_, d_]]; uv += [[[ua, 0], [ub, 0], [ub, 1]], [[ua, 0], [ub, 1], [ua, 1]]]
    return (fn, (1.0, 1.0, 1.0), 31, True), np.array(tp), np.array(uv)

def collide_boxes(s, lo, hi, cidx):
    boxes = []
    skip = [x for x in os.environ.get("BOXSKIP", "").split(",") if x]
    for gf, g in s.go.items():
        if "tf" not in g or not s.active_in_hierarchy(gf): continue
        pth = s.path(gf) if skip else ""
        if any(pth == k[:-1] if k.endswith("$") else k in pth for k in skip): continue
        for cf, c, d in s.comps(gf):
            if c != 65 or d.get("m_IsTrigger", 0) or not d.get("m_Enabled", 1): continue
            p, q, sc = s.world(g["tf"])
            cen = np.array([d["m_Center"][k] for k in "xyz"], float); size = np.array([d["m_Size"][k] for k in "xyz"], float)
            Rm = quat_mat(q)
            ext = np.abs(size * np.array(sc))
            # long rotated walls: split along the long horizontal axis so each AABB stays tight
            ax = 0 if ext[0] >= ext[2] else 2
            n = max(1, int(np.ceil(ext[ax] / 1.0))) if ext[ax] > 3 * max(0.3, ext[2 - ax]) else 1
            for k in range(n):
                a0, a1 = -0.5 + k / n, -0.5 + (k + 1) / n
                cs_ = []
                for sg in itertools.product((-1, 1), repeat=3):
                    v = np.array(sg, float) / 2
                    v[ax] = a0 if sg[ax] < 0 else a1
                    cs_.append(Rm @ ((cen + size * v) * np.array(sc)) + np.array(p))
                wc = np.array(cs_)
                mn, mx = wc.min(0), wc.max(0)
                if np.any(mx < lo) or np.any(mn > hi): continue
                ck = (int((mn[0] + mx[0]) / 2 // CS), int((mn[2] + mx[2]) / 2 // CS))
                if ck not in cidx: continue
                if (mx - mn).max() > 200: continue
                boxes.append((mn[0], mx[0], mn[1], mx[1], -mx[2], -mn[2]))
    print("boxes", len(boxes))
    return boxes

def raster_floor(f, ck, T):
    n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]); ln = np.linalg.norm(n, axis=1)
    ok = (ln > 1e-9); ny = np.where(ok, n[:, 1] / np.maximum(ln, 1e-12), 0)
    T = T[(ny > 0.55) | (ny < -0.55)]    # accept either winding for flat surfaces
    x0, z0 = ck[0] * CS, ck[1] * CS
    for t in T:
        xz = t[:, [0, 2]]
        a = np.floor((xz.min(0) - [x0, z0]) / FC).astype(int); b = np.floor((xz.max(0) - [x0, z0]) / FC).astype(int)
        a = np.maximum(a, 0); b = np.minimum(b, FN - 1)
        if a[0] > b[0] or a[1] > b[1]: continue
        gx_, gz_ = np.meshgrid(np.arange(a[0], b[0] + 1), np.arange(a[1], b[1] + 1))
        px = x0 + (gx_.ravel() + 0.5) * FC; pz = z0 + (gz_.ravel() + 0.5) * FC
        (xa, za), (xb, zb), (xc, zc) = xz
        den = (zb - zc) * (xa - xc) + (xc - xb) * (za - zc)
        if abs(den) < 1e-12: continue
        w0 = ((zb - zc) * (px - xc) + (xc - xb) * (pz - zc)) / den
        w1 = ((zc - za) * (px - xc) + (xa - xc) * (pz - zc)) / den
        w2 = 1 - w0 - w1
        ins = (w0 >= -0.05) & (w1 >= -0.05) & (w2 >= -0.05)
        y = w0 * t[0, 1] + w1 * t[1, 1] + w2 * t[2, 1]
        for cx, cz, yy in zip(gx_.ravel()[ins], gz_.ravel()[ins], y[ins]):
            if yy > f[cz, cx] and yy < 60: f[cz, cx] = yy

def emit(out, chunks, cidx, buckets, texs, tindex, boxes, floors):
    SC = 8.0
    ii = [c[0] for c in chunks]; jj = [c[1] for c in chunks]
    gi0, gj0 = min(ii), min(jj); gnx, gnz = max(ii) - gi0 + 1, max(jj) - gj0 + 1
    grid = np.full((gnz, gnx), 0xFFFF, np.uint16)
    for k, (i, j) in enumerate(chunks): grid[j - gi0 * 0 - gj0, i - gi0] = k
    blob = bytearray()
    def al():
        while len(blob) % 4: blob.extend(b"\0")
    # textures
    tex_hdr = []
    for r in texs:
        al(); po = len(blob); blob.extend(struct.pack("<%dH" % len(r["pal"]), *r["pal"]))
        al(); to = len(blob); blob.extend(r["data"])
        tex_hdr.append((r["w"], r["h"], len(r["pal"]) | (r["cut"] << 16), po, to))
    per = collections.defaultdict(list)
    for (ck, key), lst in buckets.items(): per[ck].append((key, lst))
    ch_hdr = []; tot = 0; mx = 0
    for ck in chunks:
        i, j = ck
        allp = [t for key, lst in per[ck] for t, _ in lst]
        allp = np.concatenate(allp).reshape(-1, 3) if allp else np.zeros((1, 3))
        cy = (allp[:, 1].min() + allp[:, 1].max()) / 2
        ctr = np.array([(i + 0.5) * CS, cy, (j + 0.5) * CS])
        rad = float(np.linalg.norm(allp - ctr, axis=1).max()) if len(allp) > 1 else CS
        groups = []; ctri = 0
        for key, lst in sorted(per[ck], key=lambda kv: kv[0][2] < 31):
            tp = np.concatenate([t for t, _ in lst]); uv = np.concatenate([u for _, u in lst])
            tp = tp - ctr; tp[:, :, 2] *= -1
            uv = uv.copy(); uv[:, :, 1] = 1 - uv[:, :, 1]
            e1 = tp[:, 1] - tp[:, 0]; e2 = tp[:, 2] - tp[:, 0]
            ok = np.linalg.norm(np.cross(e1, e2), axis=1) > 1e-6
            tp, uv = tp[ok], uv[ok]
            if not len(tp): continue
            ti = tindex.get((key[0], key[2] == 31 and key[3]), -1) if key[0] else -1
            w, h = (texs[ti]["w"], texs[ti]["h"]) if ti >= 0 else (1, 1)
            pos = np.clip(tp / SC, -7.99, 7.99).reshape(-1, 3)
            tris = np.arange(len(pos)).reshape(-1, 3)[:, ::-1]
            # per-triangle uv recentre keeps texcoords inside the s16 range
            u2 = uv.copy(); u2 -= np.floor(u2.min(axis=1, keepdims=True))
            cmds = gx.triangles(pos, None, u2.reshape(-1, 2) if ti >= 0 else None, tris, (w, h))
            dl = gx.encode(cmds)
            al(); dlo = len(blob); blob.extend(dl)
            c15 = gx.pack_color15(*[min(1, max(0, x)) for x in key[1]])
            groups.append((ti, c15 | (key[2] << 16), len(dl) // 4 - 1, dlo)); ctri += len(tp)
        al(); go = len(blob)
        for g in groups: blob.extend(struct.pack("<iIII", *g))
        f = floors[ck]
        fq = np.where(f < -1e8, -32768, np.clip(np.round(f * 32), -32767, 32767)).astype("<i2")
        al(); fo = len(blob); blob.extend(fq.tobytes())
        ch_hdr.append((int(ctr[0] * 4096), int(ctr[1] * 4096), int(-ctr[2] * 4096), int(SC * 4096), len(groups), go, fo, int(rad * 4096)))
        tot += ctri; mx = max(mx, ctri)
    H = 13 * 4
    grid_off = H; tex_off = grid_off + grid.nbytes; tex_off += (-tex_off) % 4
    chunk_off = tex_off + 20 * len(texs); box_off = chunk_off + 32 * len(chunks); base = box_off + 24 * len(boxes)
    hdr = b"WLD1" + struct.pack("<3I", len(texs), len(chunks), len(boxes)) + struct.pack("<3i", int(CS * 4096), int(gi0 * CS * 4096), int(gj0 * CS * 4096)) + \
        struct.pack("<6I", gnx, gnz, grid_off, tex_off, chunk_off, box_off)
    body = bytearray(hdr) + grid.tobytes()
    while len(body) < tex_off: body += b"\0"
    for t in tex_hdr: body += struct.pack("<5I", t[0], t[1], t[2], t[3] + base, t[4] + base)
    for c in ch_hdr: body += struct.pack("<4i4I", c[0], c[1], c[2], c[3], c[4], c[5] + base, c[6] + base, c[7])
    for b in boxes: body += struct.pack("<6i", *[int(round(v * 4096)) for v in b])
    assert len(body) == base
    open(out, "wb").write(bytes(body) + bytes(blob))
    print("world", out, len(body) + len(blob), "bytes; chunks", len(chunks), "tris", tot, "max/chunk", mx)

if __name__ == "__main__":
    main()
