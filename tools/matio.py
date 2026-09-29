#!/usr/bin/env python3
"""Read Unity .mat files: main texture guid, tint, tiling, render mode hints."""
import os, yaml, unity
G = unity.guid_map()
_cache = {}
TEXKEYS = ("_MainTex", "_BaseMap", "_BaseColorMap", "_Albedo", "_MainTexture", "_Diffuse", "_Tex")
COLKEYS = ("_Color", "_BaseColor", "_TintColor", "_MainColor")

def _items(x):
    if isinstance(x, dict):
        return x.items()
    out = []
    for e in x or []:
        out.extend(e.items())
    return out

def load(guid):
    if guid in _cache: return _cache[guid]
    p = G.get(guid); r = None
    if p and p.endswith(".mat"):
        txt = open(os.path.join(unity.ROOT, p), encoding="utf-8", errors="ignore").read()
        body = txt.split("--- !u!21", 1)[1].split("\n", 1)[1]
        m = yaml.load(body, Loader=unity.Loader)["Material"]
        sp = m.get("m_SavedProperties", {})
        r = {"name": m.get("m_Name", ""), "tex": None, "col": (1, 1, 1, 1), "tile": (1, 1), "off": (0, 0),
             "emis": None, "queue": m.get("m_CustomRenderQueue", -1),
             "kw": (m.get("m_ValidKeywords") or []), "shader": m.get("m_Shader", {}).get("guid"),
             "floats": dict(_items(sp.get("m_Floats")))}
        for k, v in _items(sp.get("m_TexEnvs")):
            if k in TEXKEYS and v["m_Texture"].get("guid") and r["tex"] is None:
                r["tex"] = v["m_Texture"]["guid"]
                r["tile"] = (v["m_Scale"]["x"], v["m_Scale"]["y"]); r["off"] = (v["m_Offset"]["x"], v["m_Offset"]["y"])
        for k, v in _items(sp.get("m_Colors")):
            if k in COLKEYS: r["col"] = (v["r"], v["g"], v["b"], v["a"])
            if k == "_EmissionColor": r["emis"] = (v["r"], v["g"], v["b"], v["a"])
    _cache[guid] = r
    return r
