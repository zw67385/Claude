#!/usr/bin/env python3
"""Read Unity Mesh assets (YAML, uncompressed vertex data) into numpy arrays."""
import re, os, struct
import numpy as np

# Unity VertexAttributeFormat -> (numpy dtype, bytes)
FMT = {0: ("<f4", 4), 1: ("<f2", 2), 2: ("<u1", 1), 3: ("<i1", 1), 4: ("<u2", 2), 5: ("<i2", 2),
       6: ("<u1", 1), 7: ("<i1", 1), 8: ("<u2", 2), 9: ("<i2", 2), 10: ("<u4", 4), 11: ("<i4", 4)}
CH_POS, CH_NRM, CH_TAN, CH_COL, CH_UV0, CH_UV1 = 0, 1, 2, 3, 4, 5


class Mesh:
    def __init__(self):
        self.name = ""
        self.pos = None       # (n,3) float32
        self.nrm = None       # (n,3)
        self.uv = None        # (n,2)
        self.uv1 = None
        self.col = None       # (n,4) float 0..1
        self.subs = []        # list of (n,3) int32 triangle index arrays
        self.bw = self.bi = self.bind = None


def _field(txt, key):
    m = re.search(r"^\s*%s: (.*)$" % re.escape(key), txt, re.M)
    return m.group(1).strip() if m else None


def load(path):
    txt = open(path, encoding="utf-8", errors="ignore").read()
    m = Mesh()
    m.name = _field(txt, "m_Name") or os.path.basename(path)
    # submeshes
    sm_block = txt[txt.index("m_SubMeshes:"):txt.index("m_Shapes:")]
    subs = []
    for blk in sm_block.split("- serializedVersion")[1:]:
        g = lambda k: int(re.search(r"%s: (-?\d+)" % k, blk).group(1))
        subs.append(dict(first=g("firstByte"), count=g("indexCount"), topo=g("topology"),
                         base=g("baseVertex"), fv=g("firstVertex"), vc=g("vertexCount")))
    idx_fmt = int(_field(txt, "m_IndexFormat") or 0)
    ib = bytes.fromhex(re.search(r"^\s*m_IndexBuffer: ([0-9a-f]*)$", txt, re.M).group(1))
    idx = np.frombuffer(ib, dtype="<u4" if idx_fmt else "<u2").astype(np.int64)
    vd = txt[txt.index("m_VertexData:"):txt.index("m_CompressedMesh:")]
    vcount = int(re.search(r"m_VertexCount: (\d+)", vd).group(1))
    chans = []
    for cm in re.finditer(r"- stream: (\d+)\s+offset: (\d+)\s+format: (\d+)\s+dimension: (\d+)", vd):
        g = [int(x) for x in cm.groups()]; g[3] &= 0xF; chans.append(tuple(g))
    raw_m = re.search(r"_typelessdata: ([0-9a-f]*)", vd)
    raw = bytes.fromhex(raw_m.group(1)) if raw_m else b""
    if not vcount or not raw:
        return m
    # stream layout
    strides, starts = {}, {}
    for s, o, f, d in chans:
        if d:
            strides[s] = max(strides.get(s, 0), o + d * FMT[f][1])
    off = 0
    for s in sorted(strides):
        starts[s] = off
        off += (strides[s] * vcount + 15) & ~15
    def get(ci):
        s, o, f, d = chans[ci] if ci < len(chans) else (0, 0, 0, 0)
        if not d:
            return None
        dt, sz = FMT[f]
        base = starts[s]; st = strides[s]
        buf = np.frombuffer(raw, dtype=np.uint8)[base:base + st * vcount].reshape(vcount, st)
        arr = np.ascontiguousarray(buf[:, o:o + d * sz]).view(dt).reshape(vcount, d).astype(np.float32)
        if f == 2: arr /= 255.0
        elif f == 3: arr = np.maximum(arr / 127.0, -1)
        elif f == 4: arr /= 65535.0
        elif f == 5: arr = np.maximum(arr / 32767.0, -1)
        return arr
    m.pos, m.nrm, m.col, m.uv, m.uv1 = get(CH_POS), get(CH_NRM), get(CH_COL), get(CH_UV0), get(CH_UV1)
    m.bw, m.bi = get(12), get(13)          # skinning: blend weights / bone indices (up to 4)
    bp = txt[txt.index("m_BindPose:"):txt.index("m_BoneNameHashes:")] if "m_BindPose:" in txt and "m_BoneNameHashes:" in txt else ""
    vals = [float(v) for v in re.findall(r"e[0-3][0-3]: (\S+)", bp)]
    m.bind = np.array(vals, np.float64).reshape(-1, 4, 4) if vals else None   # row-major e_rc
    if m.pos is not None:
        m.pos = m.pos[:, :3]
    if m.nrm is not None:
        m.nrm = m.nrm[:, :3]
    for sm in subs:
        if sm["topo"] != 0:  # only triangle lists (0); quads/strips are rare
            m.subs.append(np.zeros((0, 3), np.int64)); continue
        i0 = sm["first"] // (4 if idx_fmt else 2)
        t = idx[i0:i0 + sm["count"]].reshape(-1, 3) + sm["base"]
        m.subs.append(t)
    return m


if __name__ == "__main__":
    import sys
    mm = load(sys.argv[1])
    print(mm.name, None if mm.pos is None else mm.pos.shape, [len(s) for s in mm.subs],
          None if mm.uv is None else mm.uv.shape)
