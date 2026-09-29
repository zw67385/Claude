#!/usr/bin/env python3
"""Unity TerrainData reader: heightmap + splat (dominant layer) + layer textures.
terrain_tris(x0,x1,z0,z1,step) -> list of (layer_tex_guid, tile, tris (n,3,3) world, uv (n,3,2))."""
import os, re, numpy as np
import unity
CACHE = os.environ.get("F2F_CACHE", "/tmp/f2f_cache")
ASSET = "TerrainData/WatchTower Abhinav.asset"

def _load():
    os.makedirs(CACHE, exist_ok=True)
    cf = os.path.join(CACHE, "terrain.npz")
    if os.path.exists(cf):
        z = np.load(cf, allow_pickle=True)
        return z["h"], z["splat"], list(z["layers"]), z["scale"]
    txt = open(os.path.join(unity.ROOT, ASSET)).read()
    res = int(re.search(r"m_Resolution: (\d+)", txt).group(1))
    sc = re.search(r"m_Scale: \{x: ([\d.]+), y: ([\d.]+), z: ([\d.]+)\}", txt)
    scale = np.array([float(sc.group(1)), float(sc.group(2)), float(sc.group(3))])
    hx = re.search(r"m_Heights: ([0-9a-f]+)", txt).group(1)
    h = np.frombuffer(bytes.fromhex(hx), dtype="<i2").astype(np.float32).reshape(res, res) / 32766.0 * scale[1]
    lay = re.search(r"m_TerrainLayers:\n((?:    - .*\n)+)", txt).group(1)
    lguids = re.findall(r"guid: ([0-9a-f]+)", lay)
    G = unity.guid_map()
    layers = []
    for g in lguids:
        lt = open(os.path.join(unity.ROOT, G[g])).read()
        tg = re.search(r"m_DiffuseTexture: \{fileID: \d+, guid: ([0-9a-f]+)", lt).group(1)
        ts = re.search(r"m_TileSize: \{x: ([\d.]+), y: ([\d.]+)\}", lt)
        layers.append((tg, float(ts.group(1)), float(ts.group(2))))
    maps = []
    for m in re.finditer(r"m_Name: SplatAlpha (\d+)\n(?:.*\n)*?\s*m_Width: (\d+)\n\s*m_Height: (\d+)\n(?:.*\n)*?\s*_typelessdata: ([0-9a-f]+)", txt):
        w, hh = int(m.group(2)), int(m.group(3))
        a = np.frombuffer(bytes.fromhex(m.group(4)[: w * hh * 8]), dtype=np.uint8).reshape(hh, w, 4)
        maps.append(a)
    splat = np.concatenate(maps, axis=2)[:, :, : len(layers)]
    dom = np.argmax(splat, axis=2).astype(np.uint8)
    np.savez(cf, h=h, splat=dom, layers=np.array(layers, dtype=object), scale=scale)
    return h, dom, layers, scale

H, DOM, LAYERS, SCALE = _load()
RES = H.shape[0]

def height(x, z):
    fx = np.clip(x / SCALE[0], 0, RES - 1.001); fz = np.clip(z / SCALE[2], 0, RES - 1.001)
    ix = fx.astype(int); iz = fz.astype(int); tx = fx - ix; tz = fz - iz
    a = H[iz, ix] * (1 - tx) + H[iz, ix + 1] * tx
    b = H[iz + 1, ix] * (1 - tx) + H[iz + 1, ix + 1] * tx
    return a * (1 - tz) + b * tz

def layer_at(x, z):
    n = DOM.shape[0]; size = SCALE[0] * (RES - 1)
    ix = np.clip((x / size * n).astype(int), 0, n - 1); iz = np.clip((z / size * n).astype(int), 0, n - 1)
    return DOM[iz, ix]

def terrain_tris(x0, x1, z0, z1, step):
    xs = np.arange(x0, x1 + 1e-6, step); zs = np.arange(z0, z1 + 1e-6, step)
    X, Z = np.meshgrid(xs, zs); Y = height(X, Z)
    P = np.stack([X, Y, Z], axis=-1)
    tris = []
    for j in range(len(zs) - 1):
        for i in range(len(xs) - 1):
            a, b, c, d = P[j, i], P[j, i + 1], P[j + 1, i], P[j + 1, i + 1]
            # unity winding is clockwise seen from the front (above): a,c,b and b,c,d
            tris.append((a, c, b)); tris.append((b, c, d))
    tris = np.array(tris)
    ctr = tris.mean(axis=1)
    lay = layer_at(ctr[:, 0], ctr[:, 2])
    out = []
    for l in np.unique(lay):
        t = tris[lay == l]
        tg, tx, tz = LAYERS[l]
        uv = np.stack([t[:, :, 0] / tx, t[:, :, 2] / tz], axis=-1)
        uv = uv - np.floor(uv.min(axis=1, keepdims=True))   # keep texcoords small per triangle
        out.append((tg, t, uv))
    return out

if __name__ == "__main__":
    print(RES, SCALE, len(LAYERS), H.min(), H.max())
    print("h at generator", height(np.array(311.83), np.array(325.37)))
    for tg, t, uv in terrain_tris(290, 335, 290, 345, 3): print(tg, len(t))
