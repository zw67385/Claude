"""Bake the original skybox cubemaps (6-face vertical strips) into 512x64 horizon
panoramas for the DS sky cylinder: 8bpp paletted, yaw 0..360 across, pitch +55..-15 down.
Output 'SKY1', u16 w, h, u16 horizon rgb15 (fog), u16 zenith rgb15 (clear colour),
u16 pal[256], u8 px[w*h]."""
import math, struct, sys, os
import numpy as np
from PIL import Image

SRC = '/tmp/ib/FearsToFathomAssets/assets2/Assets/Cubemap/'
OUT = os.path.join(os.path.dirname(__file__), '..', 'rom', 'nitrofs', 'sky')
W, H, TOP, BOT = 512, 64, 55.0, -15.0

# name, cubemap, exposure * tint * 2 (Unity gamma-space skybox), DS lift (the LCD is dim)
SKIES = [
    ('dusk',   'Deep Dusk Equirect.png', 1.04, 2.2),                     # First Scene: drive + diner
    ('evening', 'Deep Dusk Equirect.png', 0.66, 2.6),                     # tower evening (Seq3)
    ('cold',   'Cold Night Equirect.png', 0.58, 3.0),                     # Trail Start
    ('moon',   'Night Moon High SummerSky 3.png', 0.58, 3.0),             # Camping, Trail End, tower default
    ('cloud',  'Night Moon High SimpleCloudLayer 2.png', 0.43, 3.4),      # tower Seq2
    ('moonmid', 'Night Moon Mid SimpleCloudLayer.png', 0.56, 3.0),        # tower Seq4 / Seq7
]


def sample(faces, n, d):
    x, y, z = d
    ax, ay, az = abs(x), abs(y), abs(z)
    if ax >= ay and ax >= az:
        f, sc, tc, ma = (0, -z, -y, ax) if x > 0 else (1, z, -y, ax)
    elif ay >= az:
        f, sc, tc, ma = (2, x, z, ay) if y > 0 else (3, x, -z, ay)
    else:
        f, sc, tc, ma = (4, x, -y, az) if z > 0 else (5, -x, -y, az)
    u = (sc / ma + 1) * 0.5 * (n - 1); v = (tc / ma + 1) * 0.5 * (n - 1)
    return faces[f][int(v + .5), int(u + .5)]


def bake(name, fn, expo, lift):
    im = np.asarray(Image.open(SRC + fn).convert('RGB'), dtype=np.float32) / 255.0
    n = im.shape[1]
    faces = [im[i * n:(i + 1) * n] for i in range(6)]
    out = np.zeros((H, W, 3), np.float32)
    ss = 4   # supersample: average a 4x4 grid of directions per texel
    for j in range(H):
        for i in range(W):
            acc = np.zeros(3, np.float32)
            for a in range(ss):
                for b in range(ss):
                    yaw = (i + (a + .5) / ss) / W * 2 * math.pi
                    pit = math.radians(TOP - (j + (b + .5) / ss) / H * (TOP - BOT))
                    acc += sample(faces, n, (math.sin(yaw) * math.cos(pit), math.sin(pit), math.cos(yaw) * math.cos(pit)))
            out[j, i] = acc / (ss * ss)
    out = np.clip(out * expo * lift, 0, 1)
    # a touch more saturation: the DS panel washes dusk colours out
    g = out.mean(axis=2, keepdims=True); out = np.clip(g + (out - g) * 1.35, 0, 1)
    img = Image.fromarray((out * 255).astype(np.uint8))
    q = img.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    pal = q.getpalette()[:768] + [0] * (768 - len(q.getpalette()[:768]))
    rgb15 = lambda r, g, b: (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
    horiz = (out[int(H * TOP / (TOP - BOT)) - 2:int(H * TOP / (TOP - BOT)) + 2].mean(axis=(0, 1)) * 255).astype(int)
    zen = (out[0].mean(axis=0) * 255).astype(int)
    with open(os.path.join(OUT, name + '.sky'), 'wb') as f:
        f.write(b'SKY1' + struct.pack('<HHHH', W, H, rgb15(*horiz), rgb15(*zen)))
        f.write(struct.pack('<256H', *[rgb15(pal[k * 3], pal[k * 3 + 1], pal[k * 3 + 2]) for k in range(256)]))
        f.write(bytes(q.tobytes()))
    img.save(os.path.join(sys.argv[1] if len(sys.argv) > 1 else '/tmp', name + '_pano.png'))
    print(name, 'horizon', horiz, 'zenith', zen)


os.makedirs(OUT, exist_ok=True)
for s in SKIES:
    bake(*s)
