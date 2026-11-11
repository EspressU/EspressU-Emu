#!/usr/bin/env python3
"""Build src/ios/Resources/Localizable.xcstrings from tools/l10n/strings.json and
tools/l10n/translations/<lang>.json (English text -> translation).

Usage: python3 tools/l10n/build_catalog.py
Run extract_strings.py first when UI texts changed. Missing translations show English.
"""
import glob, json, os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "src", "ios", "Resources", "Localizable.xcstrings")

def main():
    strings = json.load(open(os.path.join(HERE, "strings.json"), encoding="utf-8"))
    langs = {}
    for path in sorted(glob.glob(os.path.join(HERE, "translations", "*.json"))):
        langs[os.path.splitext(os.path.basename(path))[0]] = json.load(open(path, encoding="utf-8"))
    catalog = {"sourceLanguage": "en", "version": "1.0", "strings": {}}
    for key in strings:
        entry = {"extractionState": "manual", "localizations": {}}
        for lang, table in langs.items():
            value = table.get(key)
            if value:
                entry["localizations"][lang] = {"stringUnit": {"state": "translated", "value": value}}
        if not entry["localizations"]:
            del entry["localizations"]
        catalog["strings"][key] = entry
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(catalog, f, ensure_ascii=False, indent=2, sort_keys=True)
        f.write("\n")
    for lang, table in langs.items():
        done = sum(1 for k in strings if table.get(k))
        print(f"{lang}: {done}/{len(strings)}")

if __name__ == "__main__":
    main()
