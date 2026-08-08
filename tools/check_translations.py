#!/usr/bin/env python3
"""Audit and repair Qt translation .ts files in translations/.

Finds the most common bugs that broke es.ts and ja.ts:

* malformed plural entries: `<numerusform>` tags stored as escaped text
  (`&lt;numerusform&gt;`) inside `<translation>`, which makes `lrelease`
  abort and silently drops the language from the published index_v2.json.
* files that do not parse as XML.
* translations that drop or add source placeholders (%1, %n, ...).
* missing (`unfinished`) and obsolete (`vanished`) message counts.

Usage:
  check_translations.py [--check] [--fix] [--dir translations] [--lrelease]

--check (default) only reports; --fix also rewrites the files, repairing
the escaped numerus blocks. Run `git diff` afterwards to review.
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

NUMERUS_ENTRIES = re.compile(r"&lt;/?numerusform&gt;")
PLACEHOLDER = re.compile(r"%n|%\d+")
MESSAGE_TAG = re.compile(r"<message[^>]*?>")
MESSAGE_BLOCK = re.compile(r"<message\b.*?</message>", re.S)


def find_lrelease():
    for name in ("lrelease", "lrelease-qt6", "lrelease-qt5"):
        p = shutil.which(name)
        if p:
            return p
    return None


class Result:
    def __init__(self, name):
        self.name = name
        self.error = None
        self.malformed = 0
        self.unfinished = 0
        self.vanished = 0
        self.placeholder_bad = 0
        self.lrelease = None
        self.lrelease_error = None
        self.repaired = False

    @property
    def ok(self):
        if self.error:
            return False
        if self.malformed or self.placeholder_bad:
            return False
        if self.lrelease is not None:
            return self.lrelease
        return True


def tokens(text):
    return set(PLACEHOLDER.findall(text))


def build_lrelease(lrelease_bin, ts_path):
    with tempfile.TemporaryDirectory() as tmp:
        qm = os.path.join(tmp, "out.qm")
        proc = subprocess.run(
            [lrelease_bin, "-removeidentical", ts_path, "-qm", qm],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        stderr = proc.stderr.decode(errors="replace") if proc.stderr else ""
        ok = proc.returncode == 0 and os.path.isfile(qm) and os.path.getsize(qm) > 0
        return ok, stderr.strip().splitlines()[0] if stderr.strip() else ""


def analyze(ts_path, lrelease):
    res = Result(os.path.basename(ts_path))

    try:
        tree = ET.parse(ts_path)
    except ET.ParseError as e:
        res.error = f"XML: {e}"
        return res
    root = tree.getroot()

    for ctx in root.iter("context"):
        for msg in ctx.findall("message"):
            tr = msg.find("translation")
            if tr is None:
                continue
            ttype = tr.get("type")
            if ttype == "vanished":
                res.vanished += 1
                continue
            if ttype == "unfinished" or not "".join(tr.itertext()).strip():
                res.unfinished += 1
                continue

            src = msg.find("source")
            if src is None:
                continue
            wanted = tokens("".join(src.itertext()))
            if msg.get("numerus") == "yes":
                for form in tr.findall("numerusform"):
                    if tokens(form.text or "") != wanted:
                        res.placeholder_bad += 1
                        break
            elif tokens(tr.text or "") != wanted:
                res.placeholder_bad += 1

    with open(ts_path, encoding="utf-8") as fh:
        res.malformed = len(NUMERUS_ENTRIES.findall(fh.read())) // 2

    if lrelease:
        res.lrelease, res.lrelease_error = build_lrelease(lrelease, ts_path)
    return res


def fix(ts_path):
    with open(ts_path, encoding="utf-8") as fh:
        raw = fh.read()

    def repair(m):
        block = m.group(0)
        has_escaped = NUMERUS_ENTRIES.search(block) is not None
        has_tag = "<numerusform>" in block
        msg_attr = re.search(r'<message[^>]*?>', block).group(0)
        is_numerus = bool(re.search(r'\bnumerus\s*=\s*["\']yes["\']', msg_attr))

        block = re.sub(r"&lt;numerusform&gt;", "<numerusform>", block)
        block = re.sub(r"&lt;/numerusform&gt;", "</numerusform>", block)

        if not is_numerus and (has_escaped or has_tag):
            block = re.sub(r"<message([^>]*?)>", r'<message\1 numerus="yes">', block, count=1)

        if is_numerus and not has_escaped and not has_tag:
            tr = re.search(r"<translation([^>]*)>(.*?)</translation>", block, re.S)
            if tr:
                body = tr.group(2)
                if "unfinished" not in tr.group(1) and "vanished" not in tr.group(1) and body.strip():
                    if "<numerusform>" not in body:
                        block = (block[:tr.start()] + f"<translation{tr.group(1)}>\n            "
                                 f"<numerusform>{body.strip()}</numerusform>\n        </translation>" + block[tr.end():])
        return block

    out = MESSAGE_BLOCK.sub(repair, raw)
    if out == raw:
        return False
    with open(ts_path, "w", encoding="utf-8") as fh:
        fh.write(out)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dir", default="translations")
    group = ap.add_mutually_exclusive_group()
    group.add_argument("--check", dest="mode", action="store_const", const="check")
    group.add_argument("--fix", dest="mode", action="store_const", const="fix")
    ap.set_defaults(mode="check")
    args = ap.parse_args()

    lrelease = find_lrelease()
    if not lrelease:
        print("advertencia: lrelease no encontrado; se omiten las comprobaciones de compilacion", file=sys.stderr)

    results = [analyze(ts_path, lrelease) for ts_path in sorted(glob.glob(os.path.join(args.dir, "*.ts")))]

    num_bad = 0
    for r in results:
        if not r.ok:
            num_bad += 1
        detail = (
            f"xml={'OK' if r.error is None else 'FALLO'} "
            f"malformed_num={r.malformed} unfinished={r.unfinished} vanished={r.vanished} "
            f"placeholder_bad={r.placeholder_bad} lrelease={'yes' if r.lrelease else 'no'}"
        )
        if r.lrelease_error:
            detail += f"  err={r.lrelease_error[:80]}"
        print(f"[{'OK' if r.ok else 'PROBLEMA'}] {r.name:<14s} {detail}")

    print(f"\n{len(results)} archivos auditados, {num_bad} con problemas.")

    if args.mode == "fix":
        fixed = 0
        for ts_path in sorted(glob.glob(os.path.join(args.dir, "*.ts"))):
            if fix(ts_path):
                fixed += 1
        print(f"archivos reescritos por --fix: {fixed}")


if __name__ == "__main__":
    main()