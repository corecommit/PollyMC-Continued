#!/usr/bin/env python3
"""Bootstrap commands.json from MainWindow.ui without building.

This is a static approximation for bootstrapping the embedding table.
It cannot see runtime-only entries (account list items, theme switcher
entries added in code, visibility state), so the CI dump tool
(command-registry-dump, which calls CommandRegistry::collect() on a
live window) remains the ground truth and must regenerate this file
per release.
"""
import html
import json
import os
import re

UI_PATH = "launcher/ui/MainWindow.ui"
OUT_PATH = "training/export/commands.json"


def main():
    txt = open(UI_PATH, encoding="utf-8").read()
    commands = []
    seen = set()
    for name, body in re.findall(r'<action name="([^"]+)">(.*?)</action>', txt, re.S):
        m = re.search(r"<string[^>]*>(.*?)</string>", body, re.S)
        # same decoding the palette applies: entities first, then accelerators
        text = html.unescape(m.group(1).strip() if m else "").replace("&", "")
        if not text:
            text = name
        if name in seen:
            continue
        seen.add(name)
        commands.append({"id": name, "text": text, "category": "Other", "static": True})
    commands.sort(key=lambda c: c["id"])
    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    with open(OUT_PATH, "w", encoding="utf-8") as f:
        json.dump({"version": 1, "commands": commands}, f, ensure_ascii=False, sort_keys=True, indent=2)
        f.write("\n")
    print(f"wrote {OUT_PATH} ({len(commands)} static commands)")


if __name__ == "__main__":
    main()
