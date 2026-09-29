#!/usr/bin/env python3
"""Join Unity Localization shared-data keys with the English table text."""
import re, json, sys, os
SRC = os.environ.get("F2F", "/tmp/ib/FearsToFathomAssets/assets2/Assets/Localization/Tables")
OUT = os.path.join(os.path.dirname(__file__), "..", "data", "text_en.json")

def read_multiline(path):
    """m_Localized values may wrap over several lines; re-parse with a YAML loader."""
    import yaml
    txt = open(path, encoding="utf-8").read()
    txt = re.sub(r"^%.*$|^--- !u!.*$", "", txt, flags=re.M)
    return yaml.safe_load(txt)["MonoBehaviour"]

tables = {}
for f in sorted(os.listdir(SRC)):
    m = re.match(r"(.+)_en\.asset$", f)
    if not m: continue
    name = m.group(1)
    loc = read_multiline(os.path.join(SRC, f))
    shared = read_multiline(os.path.join(SRC, name + " Shared Data.asset"))
    keys = {e["m_Id"]: e["m_Key"] for e in shared["m_Entries"]}
    tables[name] = {keys.get(e["m_Id"], str(e["m_Id"])): e["m_Localized"] for e in loc["m_TableData"]}
    print(name, len(tables[name]), file=sys.stderr)
json.dump(tables, open(OUT, "w"), indent=1, ensure_ascii=False)
