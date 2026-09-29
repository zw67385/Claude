#!/usr/bin/env python3
"""lvlfilter.py in.bin out.bin i,j,k  -> LVL1 containing only those groups (debug)"""
import sys, struct
d = open(sys.argv[1], "rb").read(); keep = [int(x) for x in sys.argv[3].split(",")]
n = struct.unpack_from("<I", d, 4)[0]
hdr = [d[24 + 32 * i: 56 + 32 * i] for i in keep]
open(sys.argv[2], "wb").write(d[:4] + struct.pack("<I", len(keep)) + d[8:24] + b"".join(hdr) + d[24 + 32 * len(keep):24 + 32 * n] + d[24 + 32 * n:])
