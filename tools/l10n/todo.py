#!/usr/bin/env python3
"""List the English texts that still need a translation for a language.

Usage: python3 tools/l10n/todo.py <lang> [start] [count]
Writes todo/<lang>.json (the numbering txt2json.py uses) and prints `index|English`.
"""
import json, os, sys
here = os.path.dirname(os.path.abspath(__file__))
lang = sys.argv[1]
start = int(sys.argv[2]) if len(sys.argv) > 2 else 0
count = int(sys.argv[3]) if len(sys.argv) > 3 else 100000
strings = json.load(open(os.path.join(here, "strings.json"), encoding="utf-8"))
tpath = os.path.join(here, "translations", lang + ".json")
have = json.load(open(tpath, encoding="utf-8")) if os.path.exists(tpath) else {}
SAME = set()
todo = [k for k in strings if k not in have]
os.makedirs(os.path.join(here, "todo"), exist_ok=True)
json.dump(todo, open(os.path.join(here, "todo", lang + ".json"), "w", encoding="utf-8"), ensure_ascii=False)
print(len(todo), "to translate", file=sys.stderr)
for i, k in enumerate(todo):
    if start <= i < start + count:
        print(f"{i}|{k}".replace("\n", "\\n"))
