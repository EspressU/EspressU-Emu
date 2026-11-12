#!/usr/bin/env python3
"""Collect the English UI strings of the iOS app that normal users can read.

Usage: python3 tools/l10n/extract_strings.py   -> writes tools/l10n/strings.json

Developer-facing texts (logs, reports, diagnostics) stay English: see DEV_FILES and exclude.txt.
Keys are final strings as the code looks them up: locf()/locn() formats use %@ for values.
"""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(ROOT, "src", "ios", "App")

DEV_FILES = {"DiagnosticsSection.swift", "LaunchLogView.swift", "PadDiagnostics.swift", "PerformanceTuning.swift",
             "SessionReport.swift", "DisplayRouter.swift", "CoverImageLoader.swift", "Localization.swift",
             "GameLanguage.swift", "EspressUThemePresets.swift", "LegacyLogCleanup.swift", "RemovedSettingsCleanup.swift"}

LIT = r'"((?:[^"\\\n]|\\.)+)"'
PATTERNS = [
    re.compile(r'\b(?:Text|Toggle|Button|Label|Section|Picker|NavigationLink|LabeledContent|Menu|TextField|SecureField|Stepper|Link|ProgressView|DisclosureGroup)\(\s*' + LIT),
    re.compile(r'\.(?:navigationTitle|accessibilityLabel|accessibilityHint|help|confirmationDialog|alert|navigationBarTitle|searchable)\(\s*(?:title:\s*)?' + LIT),
    re.compile(r'\b(?:loc|locf)\(\s*' + LIT),
    re.compile(r'\blocn\(\s*[^,]+,\s*' + LIT + r'\s*,\s*' + LIT),
    re.compile(r'\b(?:title|detail|caption|message|subtitle|footer|header|summary|placeholder|prompt|hint|note|buttonTitle|text|label|body|headline)\s*:\s*' + LIT),
    re.compile(r'\bSettingsOption\(id:\s*"[^"]*",\s*title:\s*' + LIT),
    re.compile(r'\.(?:action|link|open|info)\(\s*' + LIT),
    re.compile(r'\breturn ' + LIT),
    re.compile(r'[?:]\s*' + LIT + r'\s*(?:[:)\]},]|$)'),
]
BAD_LINE = re.compile(r'(forKey|logger|cemu_bridge_log|cemuLog|print\(|NSLog|systemName|URL\(string|Notification\.Name|DispatchQueue|assert|fatalError|accessibilityIdentifier|UserDefaults|os_log|Logger|\blog\()')
SKIP = re.compile(r'^(?:[a-z0-9_.\-/:%@]+|\s*|[\W\d]+)$')


def unescape(s):
    s = re.sub(r'\\u\{([0-9a-fA-F]+)\}', lambda m: chr(int(m.group(1), 16)), s)
    return s.replace('\\n', '\n').replace('\\t', '\t').replace('\\"', '"').replace("\\'", "'").replace('\\\\', '\\')


def main():
    exclude = set()
    ex = os.path.join(HERE, "exclude.txt")
    if os.path.exists(ex):
        exclude = {l.rstrip("\n") for l in open(ex, encoding="utf-8") if l.strip() and not l.startswith("#")}
    found = {}
    for dp, _, files in os.walk(SRC):
        for f in files:
            if not f.endswith(".swift") or f in DEV_FILES:
                continue
            path = os.path.join(dp, f)
            for line in open(path, encoding="utf-8"):
                if line.lstrip().startswith("//"):
                    continue
                uses_loc = "loc(" in line or "locf(" in line or "locn(" in line
                if BAD_LINE.search(line) and not uses_loc:
                    continue
                for pat in PATTERNS:
                    for m in pat.finditer(line):
                        for raw in m.groups():
                            if "\\(" in raw:
                                continue
                            s = unescape(raw)
                            if SKIP.match(s) or len(s) < 2:
                                continue
                            if not (re.match(r"^[A-Z0-9(\"'\u201c.\u2026\u2192]", s) or "%@" in s or ' ' in s):
                                continue
                            if s in exclude:
                                continue
                            # prose-like or a short label; drop code-looking strings
                            if re.search(r'[_{}<>=|]|\.swift|^[A-Z][a-z]+[A-Z][a-z]+$', s) and ' ' not in s:
                                continue
                            found.setdefault(s, set()).add(os.path.relpath(path, SRC))
    extra = os.path.join(HERE, "extra_strings.txt")
    if os.path.exists(extra):
        for l in open(extra, encoding="utf-8"):
            l = l.rstrip("\n")
            if l:
                found.setdefault(l, set()).add("extra_strings.txt")
    out = os.path.join(HERE, "strings.json")
    data = {k: sorted(v) for k, v in sorted(found.items())}
    json.dump(data, open(out, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print(f"{len(data)} strings -> {out}")


if __name__ == "__main__":
    main()
