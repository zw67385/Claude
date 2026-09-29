#!/usr/bin/env python3
"""Pack data/yarn.json + text into rom/nitrofs/story.bin.
Layout: 'YRN0', u32 ninst, u32 nnodes, u32 nstr, u32 insoff, u32 nodeoff, u32 stroff, u32 blobsize
inst: 12 bytes (u8 op, u8 pad, u16 pad, i32 a, i32 b); nodes: u32 nameStr, u32 firstInst; strings: u32 offsets[] + blob."""
import json, struct, re, sys
OPS = ["JUMP_TO","JUMP","RUN_LINE","RUN_COMMAND","ADD_OPTION","SHOW_OPTIONS","PUSH_STRING","PUSH_FLOAT","PUSH_BOOL","PUSH_NULL","JUMP_IF_FALSE","POP","CALL_FUNC","PUSH_VARIABLE","STORE_VARIABLE","STOP","RUN_NODE"]
y = json.load(open("data/yarn.json")); txt = json.load(open("data/text_en.json"))["Dialogue"]
pool = {}; strs = []
def S(s):
    if s not in pool: pool[s] = len(strs); strs.append(s)
    return pool[s]
def clean(t):
    if isinstance(t, bool): return "yes" if t else "no"
    t = str(t)
    t = t.replace("<br>", "\n"); t = re.sub(r"<[^>]+>", "", t); t = t.replace("[nomarkup]", "").replace("[/nomarkup]", "")
    for a, b in {"‘": "'", "’": "'", "“": '"', "”": '"', "—": "-", "–": "-", "…": "...", " ": " "}.items(): t = t.replace(a, b)
    return t.encode("ascii", "replace").decode()
inst = []; nodes = []
for name, n in y.items():
    base = len(inst); nodes.append((S(name), base))
    lab = {k: v for k, v in n["labels"].items()}
    for i in n["ins"]:
        op = OPS.index(i["op"]); a = b = 0; args = i["args"]
        if i["op"] in ("RUN_LINE", "ADD_OPTION"):
            tx = clean(txt.get(args[0], ""))
            if i["op"] == "ADD_OPTION" and tx.endswith("Green"): tx = tx[:-5]
            a = S(tx)
            if i["op"] == "ADD_OPTION": b = base + lab[args[1]]
        elif i["op"] == "JUMP_TO": a = base + lab[args[0]]
        elif i["op"] == "JUMP_IF_FALSE": a = base + lab[args[0]]
        elif i["op"] == "RUN_COMMAND": a = S(args[0])
        elif i["op"] in ("PUSH_STRING", "PUSH_VARIABLE", "STORE_VARIABLE", "CALL_FUNC"): a = S(args[0])
        elif i["op"] == "PUSH_FLOAT": a = int(round(args[0] * 100))
        elif i["op"] == "PUSH_BOOL": a = int(args[0])
        inst.append((op, a, b))
# ADD_OPTION labels reference instruction; JUMP pops option target which we store as inst index in b
ib = b"".join(struct.pack("<BBHii", o, 0, 0, a, b) for o, a, b in inst)
nb = b"".join(struct.pack("<II", a, b) for a, b in nodes)
blob = bytearray(); offs = []
for s in strs: offs.append(len(blob)); blob += s.encode() + b"\0"
sb = struct.pack("<%dI" % len(offs), *offs)
hdr = 32 + 0
insoff = 32; nodeoff = insoff + len(ib); stroff = nodeoff + len(nb)
out = b"YRN0" + struct.pack("<7I", len(inst), len(nodes), len(strs), insoff, nodeoff, stroff, len(blob)) + ib + nb + sb + bytes(blob)
open("rom/nitrofs/story.bin", "wb").write(out)
print(len(out), "bytes", len(inst), "inst", len(nodes), "nodes", len(strs), "strings")
bad = set(c for s in strs for c in s if ord(c) > 126 or ord(c) < 32); print("odd chars", bad)
