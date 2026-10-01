#!/usr/bin/env python3
"""Offline paraphrase generator for command texts (Part 3C).

Writes training/paraphrases.json: {command_id: [variant, ...]}.

Variants are deterministic template transforms (no network, no model):
  - lowercased text
  - trailing "..." removed
  - leading verbs swapped (Add->create/new, Open->open/show, View->show/...)
  - "open <text>" fallback for short texts

Optional LLM extension: if the env var POLLYMC_PARAPHRASE_CMD names an
executable, it is invoked as `<cmd> <command-id> <text>` and its stdout
lines are appended as extra variants. Output stays deterministic only
if that command is deterministic.
"""
import json
import os
import subprocess
import sys

COMMANDS_JSON = "training/export/commands.json"
OUT_PARAPHRASES = "training/paraphrases.json"

VERB_SWAPS = [
    ("add ", ["create ", "new "]),
    ("open ", ["show ", "go to "]),
    ("view ", ["show ", "open "]),
    ("delete ", ["remove "]),
    ("launch ", ["start ", "play ", "run "]),
]


def template_variants(text):
    out = []
    low = text.lower()
    if low != text:
        out.append(low)
    no_ellipsis = text[:-3].strip() if text.endswith("...") else text
    if no_ellipsis != text:
        out.append(no_ellipsis)
        if no_ellipsis.lower() != no_ellipsis:
            out.append(no_ellipsis.lower())
    for prefix, alts in VERB_SWAPS:
        if low.startswith(prefix):
            rest = text[len(prefix):]
            for alt in alts:
                out.append(alt + rest)
            break
    else:
        if len(text) < 40 and not low.startswith(("open ", "show ")):
            out.append("open " + low)
    seen = set()
    return [v for v in out if v != text and not (v in seen or seen.add(v))]


def main():
    with open(COMMANDS_JSON, encoding="utf-8") as f:
        commands = json.load(f)["commands"]
    helper = os.environ.get("POLLYMC_PARAPHRASE_CMD", "")
    result = {}
    for cmd in commands:
        variants = template_variants(cmd["text"])
        # same %1 normalization as the embedding pipeline: never store
        # the literal placeholder token in the dataset
        variants = [v.replace("%1", "PollyMC") for v in variants]
        if helper:
            try:
                proc = subprocess.run([helper, cmd["id"], cmd["text"]], capture_output=True,
                                      text=True, timeout=60)
                for line in proc.stdout.splitlines():
                    line = line.strip()
                    if line and line != cmd["text"] and line not in variants:
                        variants.append(line)
            except Exception as e:
                print(f"warning: paraphrase helper failed for {cmd['id']}: {e}", file=sys.stderr)
        if variants:
            result[cmd["id"]] = variants
    os.makedirs(os.path.dirname(OUT_PARAPHRASES), exist_ok=True)
    with open(OUT_PARAPHRASES, "w", encoding="utf-8") as f:
        json.dump(result, f, ensure_ascii=False, sort_keys=True, indent=2)
        f.write("\n")
    total = sum(len(v) for v in result.values())
    print(f"wrote {OUT_PARAPHRASES} ({len(result)} commands, {total} variants)")


if __name__ == "__main__":
    main()
