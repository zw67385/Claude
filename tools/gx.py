#!/usr/bin/env python3
"""Encode triangle meshes as Nintendo DS geometry-engine display lists (glCallList format)."""
import struct
import numpy as np

CMD_NOP, CMD_COLOR, CMD_NORMAL, CMD_TEXCOORD = 0x00, 0x20, 0x21, 0x22
CMD_VTX16, CMD_BEGIN, CMD_END = 0x23, 0x40, 0x41


def pack_normal(n):
    n = np.clip(np.asarray(n, dtype=np.float64), -1, 1)
    v = np.clip(np.round(n * 511.0), -512, 511).astype(np.int32) & 0x3FF
    return int(v[0] | (v[1] << 10) | (v[2] << 20))


def pack_color15(r, g, b):
    return (int(r * 31 + .5) & 31) | ((int(g * 31 + .5) & 31) << 5) | ((int(b * 31 + .5) & 31) << 10)


def encode(cmds):
    """cmds: list of (opcode, [param words]).  Returns bytes: count word + packed stream."""
    words = []
    for i in range(0, len(cmds), 4):
        grp = cmds[i:i + 4]
        packed = 0
        for j, (op, _) in enumerate(grp):
            packed |= op << (8 * j)
        words.append(packed)
        for _, params in grp:
            words.extend(params)
    return struct.pack("<I%dI" % len(words), len(words), *[w & 0xFFFFFFFF for w in words])


def triangles(pos, nrm, uv, tris, tex_size, flat=True, color=None):
    """pos (n,3) in model space already scaled to |v|<8, uv (n,2) in 0..1+ (unit tiles),
    tris (m,3). tex_size=(w,h) to convert uv to 12.4 texel units. Returns list of commands."""
    cmds = [(CMD_BEGIN, [0])]
    w, h = tex_size
    last_n = None
    q = np.clip(np.round(pos * 4096.0), -32768, 32767).astype(np.int32)
    for t in tris:
        if flat:
            a, b, c = pos[t[0]], pos[t[1]], pos[t[2]]
            fn = np.cross(b - a, c - a)
            ln = np.linalg.norm(fn)
            fn = fn / ln if ln > 1e-12 else np.array([0, 1.0, 0])
            pn = pack_normal(fn)
            if pn != last_n:
                cmds.append((CMD_NORMAL, [pn])); last_n = pn
        for vi in t:
            if not flat:
                pn = pack_normal(nrm[vi])
                if pn != last_n:
                    cmds.append((CMD_NORMAL, [pn])); last_n = pn
            if uv is not None:
                s = int(np.clip(round(uv[vi, 0] * w * 16), -32768, 32767)) & 0xFFFF
                tt = int(np.clip(round(uv[vi, 1] * h * 16), -32768, 32767)) & 0xFFFF
                cmds.append((CMD_TEXCOORD, [s | (tt << 16)]))
            x, y, z = q[vi]
            cmds.append((CMD_VTX16, [(x & 0xFFFF) | ((y & 0xFFFF) << 16), z & 0xFFFF]))
    cmds.append((CMD_END, []))
    return cmds
