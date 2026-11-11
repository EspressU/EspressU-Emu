#!/usr/bin/env python3
"""Merge hand-written translation lines into translations/<lang>.json.

Usage: python3 tools/l10n/txt2json.py <lang> <txt-file>... [--keys keys.json]
Each line is `index|translation`. Indexes refer to the list written by todo.py
(todo/<lang>.json) unless --keys points to another list of English keys.
Leading and trailing spaces of the English key are copied to the translation.
"""
import json, os, sys

here = os.path.dirname(os.path.abspath(__file__))
args = sys.argv[1:]
keys_path = None
if "--keys" in args:
    i = args.index("--keys"); keys_path = args[i + 1]; del args[i:i + 2]
lang, files = args[0], args[1:]
if keys_path is None:
    keys_path = os.path.join(here, "todo", lang + ".json")
keys = json.load(open(keys_path, encoding="utf-8"))
if isinstance(keys, dict):
    keys = list(keys)
tpath = os.path.join(here, "translations", lang + ".json")
out = json.load(open(tpath, encoding="utf-8")) if os.path.exists(tpath) else {}
n = 0
for p in files:
    for line in open(p, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line.strip():
            continue
        i, _, t = line.partition("|")
        k = keys[int(i)]
        t = t.replace("\\n", "\n").strip()
        lead = k[:len(k) - len(k.lstrip())]
        trail = k[len(k.rstrip()):]
        out[k] = lead + t + trail
        n += 1
os.makedirs(os.path.dirname(tpath), exist_ok=True)
json.dump(out, open(tpath, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print(lang, "merged", n, "total", len(out))
