#!/usr/bin/env python3
"""Offline preview of LVL1 levels as the DS would draw them: decodes the display lists, renders a
z-buffered image coloured by texture/material (or group id) and counts the polygons that survive
back-face culling and frustum rejection (the DS limit is 2048 per frame).
usage: preview.py out.png px py pz yaw pitch level.bin [level.bin...]   (px.. in metres, ORIGIN frame; yaw/pitch DS units)
env IDS=1 colours by group id."""
import sys, os, struct
import numpy as np
from PIL import Image
ORG = np.array([315.0, 45.25, -327.0])

def load(fn):
    d = open(fn, "rb").read()
    n = struct.unpack_from("<I", d, 4)[0]
    sc, cx, cy, cz = struct.unpack_from("<Iiii", d, 8)
    off = np.array([cx, cy, cz]) / 4096 - ORG
    groups = []
    for i in range(n):
        w, h, col, fl, dw, po, to, do = struct.unpack_from("<8I", d, 24 + 32 * i)
        words = struct.unpack_from("<%dI" % (dw + 1), d, do)[1:]
        verts, uvs, k = [], [], 0; cur_uv = (0, 0); prim = 0; pv = []
        def flush():
            if prim == 1:
                for i in range(0, len(pv) - 3, 4):
                    a, b, c, d = pv[i:i + 4]; verts.extend([a[0], b[0], c[0], a[0], c[0], d[0]]); uvs.extend([a[1], b[1], c[1], a[1], c[1], d[1]])
            else:
                for a in pv: verts.append(a[0]); uvs.append(a[1])
            pv.clear()
        while k < len(words):
            ops = words[k]; k += 1
            for j in range(4):
                op = (ops >> (8 * j)) & 0xFF
                if op == 0x23:
                    a, b = words[k], words[k + 1]; k += 2
                    v = [((a & 0xFFFF) ^ 0x8000) - 0x8000, ((a >> 16) ^ 0x8000) - 0x8000, ((b & 0xFFFF) ^ 0x8000) - 0x8000]
                    pv.append((v, cur_uv))
                elif op == 0x22:
                    a = words[k]; k += 1
                    cur_uv = ((((a & 0xFFFF) ^ 0x8000) - 0x8000) / 16 / max(w, 1), (((a >> 16) ^ 0x8000) - 0x8000) / 16 / max(h, 1))
                elif op == 0x40: flush(); prim = words[k]; k += 1
                elif op == 0x41: flush()
                elif op in (0x20, 0x21): k += 1
        flush()
        v = np.array(verts, float).reshape(-1, 3, 3) / 4096 * (sc / 4096) + off
        tex = None
        if fl & 1:
            npal = (fl >> 8) & 0x1FF
            pal = np.array(struct.unpack_from("<%dH" % npal, d, po))
            rgb = np.stack([(pal & 31), (pal >> 5) & 31, (pal >> 10) & 31], -1) * 8
            idx = np.frombuffer(d, np.uint8, w * h, to).reshape(h, w)
            tex = rgb[np.minimum(idx, npal - 1)]
        c = np.array([col & 31, (col >> 5) & 31, (col >> 10) & 31]) * 8
        groups.append((v, np.array(uvs).reshape(-1, 3, 2), tex, c, fl))
    return groups

def render(groups, eye, yaw, pitch, W=256, H=192, ids=False):
    ya, pa = yaw / 32768 * 2 * np.pi, pitch / 32768 * 2 * np.pi
    cy_, sy = np.cos(-ya), np.sin(-ya)
    Ry = np.array([[cy_, 0, sy], [0, 1, 0], [-sy, 0, cy_]])
    cp, sp = np.cos(pa), np.sin(pa)
    Rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
    M = Rx @ Ry
    f = 1 / np.tan(np.radians(70) / 2)
    img = np.zeros((H, W, 3), np.uint8); img[:] = (16, 16, 24); zb = np.full((H, W), np.inf)
    total = 0; L = np.array([0.4, -0.8, -0.3]); L /= np.linalg.norm(L)
    for gi, (v, uv, tex, col, fl) in enumerate(groups):
        cam = (v - eye) @ M.T                 # view space, looking down -z
        n = np.cross(cam[:, 1] - cam[:, 0], cam[:, 2] - cam[:, 0])
        front = (n * cam[:, 0]).sum(1) < 0
        if os.environ.get("CULL") == "0": front[:] = True
        if os.environ.get("CULL") == "rev": front = ~front
        z = -cam[:, :, 2]
        x = cam[:, :, 0] * f / (W / H) / np.maximum(z, 1e-3); y = cam[:, :, 1] * f / np.maximum(z, 1e-3)
        vis = front & (z.max(1) > 0.05) & (z.min(1) < 80) & ~np.all(x < -1, 1) & ~np.all(x > 1, 1) & ~np.all(y < -1, 1) & ~np.all(y > 1, 1)
        total += vis.sum()
        wn = np.cross(v[:, 1] - v[:, 0], v[:, 2] - v[:, 0]); wn /= np.linalg.norm(wn, axis=1, keepdims=True) + 1e-12
        shade = 12 / 31 + np.maximum(0, -(wn @ L)) * (1 - 12 / 31)
        for t in np.nonzero(vis & (z.min(1) > 0.05))[0]:
            sx = (x[t] + 1) * W / 2; sy2 = (1 - y[t]) * H / 2
            x0, x1 = int(max(0, np.floor(sx.min()))), int(min(W - 1, np.ceil(sx.max())))
            y0, y1 = int(max(0, np.floor(sy2.min()))), int(min(H - 1, np.ceil(sy2.max())))
            if x0 > x1 or y0 > y1: continue
            X, Y = np.meshgrid(np.arange(x0, x1 + 1) + .5, np.arange(y0, y1 + 1) + .5)
            (ax, ay), (bx, by), (cx, cy) = zip(sx, sy2)
            den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(den) < 1e-9: continue
            w0 = ((by - cy) * (X - cx) + (cx - bx) * (Y - cy)) / den
            w1 = ((cy - ay) * (X - cx) + (ax - cx) * (Y - cy)) / den
            w2 = 1 - w0 - w1
            m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not m.any(): continue
            iz = w0 / z[t, 0] + w1 / z[t, 1] + w2 / z[t, 2]
            dep = 1 / iz
            yy, xx = (Y[m] - .5).astype(int), (X[m] - .5).astype(int)
            ok = dep[m] < zb[yy, xx]
            yy, xx = yy[ok], xx[ok]
            zb[yy, xx] = dep[m][ok]
            if ids:
                c = np.array([(gi * 97) % 256, (gi * 57) % 256, (gi * 31 + 80) % 256])
                img[yy, xx] = c
            elif tex is not None:
                u = (w0 * uv[t, 0, 0] / z[t, 0] + w1 * uv[t, 1, 0] / z[t, 1] + w2 * uv[t, 2, 0] / z[t, 2]) / iz
                vv = (w0 * uv[t, 0, 1] / z[t, 0] + w1 * uv[t, 1, 1] / z[t, 1] + w2 * uv[t, 2, 1] / z[t, 2]) / iz
                th, tw = tex.shape[:2]
                c = tex[(vv[m][ok] * th).astype(int) % th, (u[m][ok] * tw).astype(int) % tw] * col / 248
                img[yy, xx] = np.clip(c * shade[t], 0, 255)
            else:
                img[yy, xx] = np.clip(col * shade[t], 0, 255)
    return img, total

if __name__ == "__main__":
    out = sys.argv[1]; px, py, pz, yaw, pitch = [float(a) for a in sys.argv[2:7]]
    groups = sum((load(f) for f in sys.argv[7:]), [])
    img, n = render(groups, np.array([px, py, pz]), yaw, pitch, ids=bool(os.environ.get("IDS")))
    Image.fromarray(img).resize((512, 384), Image.NEAREST).save(out)
    print("visible polys", n)
