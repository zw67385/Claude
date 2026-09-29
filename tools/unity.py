#!/usr/bin/env python3
"""Minimal Unity YAML scene/asset reader (AssetRipper-style flattened exports)."""
import os, re, pickle, sys
import yaml

ROOT = os.environ.get("F2F_ASSETS", "/tmp/ib/FearsToFathomAssets/assets2/Assets")
CACHE = os.environ.get("F2F_CACHE", "/tmp/f2f_cache")
os.makedirs(CACHE, exist_ok=True)
Loader = getattr(yaml, "CSafeLoader", yaml.SafeLoader)

_guid_re = re.compile(r"^guid: ([0-9a-f]{32})", re.M)


def guid_map():
    """guid -> path relative to ROOT (for every asset with a .meta)."""
    p = os.path.join(CACHE, "guids.pkl")
    if os.path.exists(p):
        return pickle.load(open(p, "rb"))
    m = {}
    for dp, _, fns in os.walk(ROOT):
        for fn in fns:
            if fn.endswith(".meta"):
                with open(os.path.join(dp, fn), encoding="utf-8", errors="ignore") as f:
                    g = _guid_re.search(f.read(400))
                if g:
                    m[g.group(1)] = os.path.relpath(os.path.join(dp, fn[:-5]), ROOT)
    pickle.dump(m, open(p, "wb"))
    return m


_doc_split = re.compile(r"^--- !u!(\d+) &(-?\d+)( stripped)?\s*$", re.M)


def load_docs(path):
    """Return {fileID: (classID, body_dict)} for a Unity YAML file."""
    txt = open(path, encoding="utf-8", errors="ignore").read()
    parts = _doc_split.split(txt)
    docs = {}
    # parts: [preamble, cls, fid, stripped, body, cls, fid, stripped, body...]
    for i in range(1, len(parts), 4):
        cls, fid, body = int(parts[i]), int(parts[i + 1]), parts[i + 3]
        try:
            d = yaml.load(body, Loader=Loader)
        except Exception as e:  # pragma: no cover
            continue
        if isinstance(d, dict) and len(d) == 1:
            d = next(iter(d.values()))
        docs[fid] = (cls, d)
    return docs


def load_scene(name):
    p = os.path.join(CACHE, "scene_%s.pkl" % name.replace(" ", "_"))
    src = os.path.join(ROOT, "Scenes", name + ".unity")
    if os.path.exists(p) and os.path.getmtime(p) > os.path.getmtime(src):
        return pickle.load(open(p, "rb"))
    d = load_docs(src)
    pickle.dump(d, open(p, "wb"), protocol=4)
    return d


CLS = {1: "GameObject", 4: "Transform", 20: "Camera", 23: "MeshRenderer", 33: "MeshFilter",
       54: "Rigidbody", 64: "MeshCollider", 65: "BoxCollider", 81: "AudioListener", 82: "AudioSource",
       108: "Light", 114: "MonoBehaviour", 135: "SphereCollider", 136: "CapsuleCollider",
       198: "ParticleSystem", 199: "ParticleSystemRenderer", 205: "LODGroup", 212: "SpriteRenderer",
       218: "Terrain", 224: "RectTransform", 222: "CanvasRenderer", 223: "Canvas", 95: "Animator",
       111: "Animation", 120: "LineRenderer", 137: "SkinnedMeshRenderer", 143: "CharacterController",
       178: "BillboardAsset", 96: "TrailRenderer", 102: "TextMesh", 128: "Font", 104: "RenderSettings",
       157: "LightmapSettings", 196: "NavMeshSettings", 1001: "PrefabInstance"}


def fid(ref):
    return ref.get("fileID", 0) if isinstance(ref, dict) else 0


def quat_mul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw*bx + ax*bw + ay*bz - az*by, aw*by - ax*bz + ay*bw + az*bx,
            aw*bz + ax*by - ay*bx + az*bw, aw*bw - ax*bx - ay*by - az*bz)


def quat_rot(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    # v' = v + 2*cross(q.xyz, cross(q.xyz, v) + w*v)
    cx = y*vz - z*vy + w*vx; cy = z*vx - x*vz + w*vy; cz = x*vy - y*vx + w*vz
    return (vx + 2*(y*cz - z*cy), vy + 2*(z*cx - x*cz), vz + 2*(x*cy - y*cx))


class Scene:
    """Flattened scene with resolved GameObject hierarchy and world transforms."""

    def __init__(self, name):
        self.name = name
        self.docs = load_scene(name)
        self.go = {}        # fid -> dict(name, active, comps=[fid], tf=fid, layer, tag)
        self.tf = {}        # transform fid -> dict(go, parent, pos, rot, scale)
        for f, (c, d) in self.docs.items():
            if c == 1:
                self.go[f] = {"name": d.get("m_Name", ""), "active": d.get("m_IsActive", 1),
                              "comps": [fid(x["component"]) for x in d.get("m_Component", [])],
                              "layer": d.get("m_Layer", 0), "tag": d.get("m_TagString", "")}
        for f, (c, d) in self.docs.items():
            if c in (4, 224):
                p = d["m_LocalPosition"] if "m_LocalPosition" in d else {"x": 0, "y": 0, "z": 0}
                r = d.get("m_LocalRotation", {"x": 0, "y": 0, "z": 0, "w": 1})
                s = d.get("m_LocalScale", {"x": 1, "y": 1, "z": 1})
                self.tf[f] = {"go": fid(d["m_GameObject"]), "parent": fid(d.get("m_Father", {})),
                              "pos": (float(p["x"]), float(p["y"]), float(p["z"])), "rot": (float(r["x"]), float(r["y"]), float(r["z"]), float(r["w"])),
                              "scale": (float(s["x"]), float(s["y"]), float(s["z"])), "rt": c == 224, "children": [fid(x) for x in d.get("m_Children", [])]}
        for f, t in self.tf.items():
            if t["go"] in self.go:
                self.go[t["go"]]["tf"] = f
        self._world = {}

    def world(self, tf):
        """(pos, rot quat, scale) in world space."""
        if tf in self._world:
            return self._world[tf]
        t = self.tf[tf]
        if not t["parent"] or t["parent"] not in self.tf:
            res = (t["pos"], t["rot"], t["scale"])
        else:
            pp, pr, ps = self.world(t["parent"])
            lp = (t["pos"][0]*ps[0], t["pos"][1]*ps[1], t["pos"][2]*ps[2])
            rp = quat_rot(pr, lp)
            res = ((pp[0]+rp[0], pp[1]+rp[1], pp[2]+rp[2]), quat_mul(pr, t["rot"]),
                   (ps[0]*t["scale"][0], ps[1]*t["scale"][1], ps[2]*t["scale"][2]))
        self._world[tf] = res
        return res

    def path(self, go_fid):
        names = []
        cur = self.go[go_fid].get("tf")
        while cur:
            g = self.tf[cur]["go"]
            names.append(str(self.go[g]["name"]) if g in self.go else "?")
            cur = self.tf[cur]["parent"]
            if cur not in self.tf:
                break
        return "/".join(reversed(names))

    def active_in_hierarchy(self, go_fid):
        cur = self.go[go_fid].get("tf")
        while cur:
            g = self.tf[cur]["go"]
            if g in self.go and not self.go[g]["active"]:
                return False
            cur = self.tf[cur]["parent"]
            if cur not in self.tf:
                break
        return True

    def comps(self, go_fid, cls=None):
        out = []
        for cf in self.go[go_fid]["comps"]:
            if cf in self.docs and (cls is None or self.docs[cf][0] == cls):
                out.append((cf, self.docs[cf][0], self.docs[cf][1]))
        return out


if __name__ == "__main__":
    g = guid_map()
    print(len(g), "guids")
    s = Scene(sys.argv[1] if len(sys.argv) > 1 else "Watchtower")
    print(len(s.go), "gameobjects", len(s.tf), "transforms")
