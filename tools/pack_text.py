#!/usr/bin/env python3
"""pack_text.py text_en.json out.bin -- flat string table for the DS: 'TXT1', u32 n, then n x ("table.key\\0value\\0").
Rich-text tags are stripped, non-ASCII folded to ASCII. Dialogue is packed separately (story.bin)."""
import sys, json, re, struct, unicodedata
def asc(s):
    s = re.sub(r"<[^>]*>", "", s).replace("’", "'").replace("‘", "'").replace("“", '"').replace("”", '"').replace("…", "...")
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode()
    return s.strip()
d = json.load(open(sys.argv[1], encoding="utf-8"))
out = []
for t, v in d.items():
    if t == "Dialogue" or not v: continue
    for k, s in v.items(): out.append((t + "." + k.strip(), asc(s)))
with open(sys.argv[2], "wb") as f:
    f.write(b"TXT1" + struct.pack("<I", len(out)))
    for k, s in out: f.write(k.encode() + b"\0" + s.encode() + b"\0")
print(len(out), "strings")
