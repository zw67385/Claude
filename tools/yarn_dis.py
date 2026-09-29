#!/usr/bin/env python3
"""Decode a Yarn Spinner 2 compiled program (protobuf hex in a Unity asset) to JSON."""
import re, sys, json, os, struct
SRC = os.environ.get("YARN", "/tmp/ib/FearsToFathomAssets/assets2/Assets/MonoBehaviour/Project.asset")
OUT = os.path.join(os.path.dirname(__file__), "..", "data", "yarn.json")
OPS = ["JUMP_TO","JUMP","RUN_LINE","RUN_COMMAND","ADD_OPTION","SHOW_OPTIONS","PUSH_STRING","PUSH_FLOAT",
       "PUSH_BOOL","PUSH_NULL","JUMP_IF_FALSE","POP","CALL_FUNC","PUSH_VARIABLE","STORE_VARIABLE","STOP","RUN_NODE"]

def varint(b, i):
    r = s = 0
    while True:
        c = b[i]; i += 1; r |= (c & 0x7f) << s; s += 7
        if not c & 0x80: return r, i

def fields(b):
    i, out = 0, []
    while i < len(b):
        k, i = varint(b, i); f, w = k >> 3, k & 7
        if w == 0: v, i = varint(b, i)
        elif w == 1: v = b[i:i+8]; i += 8
        elif w == 2: n, i = varint(b, i); v = b[i:i+n]; i += n
        elif w == 5: v = b[i:i+4]; i += 4
        else: raise ValueError("wire %d" % w)
        out.append((f, w, v))
    return out

def operand(b):
    for f, w, v in fields(b):
        if f == 1: return v.decode()
        if f == 2: return bool(v)
        if f == 3: return struct.unpack("<f", v)[0] if w == 5 else struct.unpack("<d", v)[0]
        if f == 4: return v if isinstance(v, int) else None
    return None

def node(b):
    n = {"name": "", "ins": [], "labels": {}, "tags": []}
    for f, w, v in fields(b):
        if f == 1: n["name"] = v.decode()
        elif f == 2:
            ins = {"op": 0, "args": []}
            for f2, w2, v2 in fields(v):
                if f2 == 1: ins["op"] = v2
                elif f2 == 2: ins["args"].append(operand(v2))
            ins["op"] = OPS[ins["op"]]
            n["ins"].append(ins)
        elif f == 3:
            k = val = None
            for f2, w2, v2 in fields(v):
                if f2 == 1: k = v2.decode()
                elif f2 == 2: val = v2
            n["labels"][k] = val
        elif f == 4: n["tags"].append(v.decode())
    return n

txt = open(SRC, encoding="utf-8").read()
hexs = re.search(r"compiledYarnProgram: ([0-9a-f]+)", txt).group(1)
prog = bytes.fromhex(hexs)
nodes = {}
for f, w, v in fields(prog):
    if f == 2:
        entry = fields(v)
        for f2, w2, v2 in entry:
            if f2 == 2:
                n = node(v2); nodes[n["name"]] = n
json.dump(nodes, open(OUT, "w"), indent=1)
tot = sum(len(n["ins"]) for n in nodes.values())
print(len(nodes), "nodes,", tot, "instructions")
from collections import Counter
print(Counter(i["op"] for n in nodes.values() for i in n["ins"]))
