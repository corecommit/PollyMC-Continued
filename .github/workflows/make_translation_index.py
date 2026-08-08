#!/usr/bin/env python3
"""Build index_v2.json for PollyMC-Continued translations.

Usage: make_translation_index.py <dir-with-mm-*.qm-and-ts>
Reads the .ts sources next to the .qm files, counts translated/fuzzy/
untranslated messages, computes sha1/size of the .qm files, and writes
index_v2.json in the same directory.
"""
import hashlib
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

NS = ""

def parse_ts(path):
    root = ET.parse(path).getroot()
    translated = untranslated = fuzzy = 0
    for context in root.findall(f"{NS}context"):
        for msg in context.findall(f"{NS}message"):
            transl = msg.find(f"{NS}translation")
            if transl is None:
                continue
            text = "".join(transl.itertext())
            ttype = transl.get("type")
            if ttype == "vanished":
                continue
            if ttype == "unfinished" or not text.strip():
                untranslated += 1
            elif ttype == "fuzzy":
                fuzzy += 1
            else:
                translated += 1
    return translated, untranslated, fuzzy

def main():
    d = sys.argv[1]
    languages = {}
    for f in os.listdir(d):
        if not (f.startswith("mmc_") and f.endswith(".qm")):
            continue
        key = f[4:-3]
        ts = os.path.join(d, f"{key}.ts")
        if not os.path.exists(ts):
            # index should only contain languages we have source stats for
            continue
        qm_path = os.path.join(d, f)
        sha1 = hashlib.sha1(open(qm_path, "rb").read()).hexdigest()
        size = os.path.getsize(qm_path)
        translated, untranslated, fuzzy = parse_ts(ts)
        languages[key] = {
            "file": f,
            "sha1": sha1,
            "size": size,
            "translated": translated,
            "untranslated": untranslated,
            "fuzzy": fuzzy,
        }
    index = {
        "file_type": "MMC-TRANSLATION-INDEX",
        "version": 2,
        "languages": languages,
    }
    out = os.path.join(d, "index_v2.json")
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(index, fh, ensure_ascii=False, sort_keys=True)
    print(f"wrote {out} with {len(languages)} languages")

if __name__ == "__main__":
    main()
