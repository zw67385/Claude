#!/usr/bin/env python3
"""Render the decoded Yarn nodes as readable script text (English)."""
import json, os, sys
D = os.path.join(os.path.dirname(__file__), "..", "data")
y = json.load(open(os.path.join(D, "yarn.json")))
t = json.load(open(os.path.join(D, "text_en.json")))["Dialogue"]
def line(a): return t.get(a, "<%s>" % a)
out = []
for name, n in y.items():
    out.append("=== NODE %s ===" % name)
    inv = {}
    for l, idx in n["labels"].items():
        if idx is not None: inv.setdefault(idx, []).append(l)
    for pc, i in enumerate(n["ins"]):
        for l in inv.get(pc, []): out.append("  %s:" % l)
        op, a = i["op"], i["args"]
        if op == "RUN_LINE": s = "SAY   %r" % line(a[0])
        elif op == "ADD_OPTION": s = "OPT   %r -> %s%s" % (line(a[0]), a[1], "  [cond]" if a[3] else "")
        elif op == "RUN_COMMAND": s = "CMD   <<%s>>" % a[0]
        else: s = "%s %s" % (op, " ".join(map(repr, a)))
        out.append("    %3d %s" % (pc, s))
open(os.path.join(D, "story_dump.txt"), "w").write("\n".join(out))
print(len(out), "lines")
