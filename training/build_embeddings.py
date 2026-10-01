#!/usr/bin/env python3
"""Build the multilingual command embedding table for the palette.

Inputs:
  training/export/commands.json   command dump (Part 3D or tools/gen_commands.py)
  translations/*.ts               Qt Linguist catalogs

Outputs (deterministic: same inputs -> byte-identical outputs):
  training/export/embeddings.json {version, model, dim, entries: {id: {lang: [...]}}}
  training/export/labels.json     {version, commands: [{id, text, category}]}

Pairing rules:
  - English variant is always the command text itself ("en").
  - A .ts pairing is used only if the translation exists, is finished
    (no type="unfinished"), and is non-empty.
  - If the tokenizer produces all [UNK] for a text, that language is
    skipped for that command and logged. Languages skipped for every
    command are reported at the end.
  - Every stored vector is L2-normalized.

The embedder backend is isolated in embed_texts() so the pairing and
serialization logic can be smoke-tested without torch installed.
"""
import json
import math
import os
import sys
import xml.etree.ElementTree as ET

MODEL_NAME = "paraphrase-multilingual-MiniLM-L12-v2"
MODEL_DIM = 384
UNK_ID = 3  # [UNK] in the XLM-R sentencepiece vocabulary

COMMANDS_JSON = "training/export/commands.json"
TRANSLATIONS_GLOB_DIR = "translations"
OUT_EMBEDDINGS = "training/export/embeddings.json"
OUT_LABELS = "training/export/labels.json"


def load_commands(path=COMMANDS_JSON):
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    return data["commands"]


def load_translations(ts_dir=TRANSLATIONS_GLOB_DIR):
    """Map (source_text) -> {lang: translated_text} for finished entries."""
    import glob
    table = {}
    for path in sorted(glob.glob(os.path.join(ts_dir, "*.ts"))):
        lang = os.path.splitext(os.path.basename(path))[0]
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError as e:
            print(f"warning: cannot parse {path}: {e}", file=sys.stderr)
            continue
        for ctx in root.findall("context"):
            for msg in ctx.findall("message"):
                src = msg.find("source")
                trn = msg.find("translation")
                if src is None or trn is None:
                    continue
                if trn.get("type") in ("unfinished", "vanished"):
                    continue
                text = "".join(trn.itertext()).strip()
                if not text:
                    continue
                table.setdefault(src.text or "", {})[lang] = text
    return table


def l2_normalize(vec):
    norm = math.sqrt(sum(x * x for x in vec))
    if norm == 0:
        return vec
    return [x / norm for x in vec]


def sanitize_for_embedding(text):
    # Runtime substitutes %1 (the app display name) before the user ever
    # sees it. Embedding the literal "%1" token would mismatch everything
    # users say, so normalize to the generic product name first.
    return text.replace("%1", "PollyMC")


def is_all_unk(token_ids):
    return len(token_ids) > 0 and all(t == UNK_ID for t in token_ids)


def build_table(commands, translations, embed_texts, tokenize):
    """Pure pairing logic. embed_texts(texts) -> list of vectors.
    tokenize(text) -> list of token ids (for the UNK check)."""
    entries = {}
    skipped_langs = set()
    seen_langs = set()
    for cmd in commands:
        cmd_id = cmd["id"]
        variants = {"en": sanitize_for_embedding(cmd["text"])}
        for lang, text in translations.get(cmd["text"], {}).items():
            seen_langs.add(lang)
            clean = sanitize_for_embedding(text)
            if is_all_unk(tokenize(clean)):
                skipped_langs.add(lang)
                print(f"warning: all-[UNK] for command {cmd_id} in {lang}, skipping pairing")
                continue
            variants[lang] = clean
        texts = list(variants.values())
        vectors = embed_texts(texts)
        assert len(vectors) == len(texts), "embedder returned wrong vector count"
        entries[cmd_id] = {lang: l2_normalize(vec) for lang, vec in zip(variants.keys(), vectors)}
    fully_skipped = {lang for lang in seen_langs if all(lang not in v for v in entries.values())}
    if fully_skipped:
        print(f"warning: languages skipped for every command: {sorted(fully_skipped)}")
    return entries


def write_json_deterministic(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        f.write("\n")


def main():
    # Heavy imports only needed for a real run, not for --check.
    from sentence_transformers import SentenceTransformer

    commands = load_commands()
    translations = load_translations()

    model = SentenceTransformer(MODEL_NAME)
    tokenizer = model.tokenizer

    def tokenize(text):
        return tokenizer.encode(text, add_special_tokens=False)

    def embed_texts(texts):
        return model.encode(texts, normalize_embeddings=True).tolist()

    entries = build_table(commands, translations, embed_texts, tokenize)

    # Optional 3C paraphrases: extra vectors under "{id}~{i}" keys,
    # stored under "en" (matching is language-agnostic, so the key is
    # informational only). The runtime matcher strips the "~" suffix.
    paraphrase_path = "training/paraphrases.json"
    if os.path.exists(paraphrase_path):
        with open(paraphrase_path, encoding="utf-8") as f:
            paraphrases = json.load(f)
        extra = 0
        for cmd in commands:
            for i, variant in enumerate(paraphrases.get(cmd["id"], [])):
                clean = sanitize_for_embedding(variant)
                if is_all_unk(tokenize(clean)):
                    continue
                vec = l2_normalize(embed_texts([clean])[0])
                entries[f"{cmd['id']}~{i}"] = {"en": vec}
                extra += 1
        print(f"embedded {extra} paraphrase variants")

    write_json_deterministic(OUT_EMBEDDINGS, {
        "version": 1,
        "model": MODEL_NAME,
        "dim": MODEL_DIM,
        "entries": entries,
    })
    write_json_deterministic(OUT_LABELS, {
        "version": 1,
        "commands": [{"id": c["id"], "text": c["text"], "category": c.get("category", "Other")}
                     for c in commands],
    })
    print(f"wrote {OUT_EMBEDDINGS} ({len(entries)} commands) and {OUT_LABELS}")


if __name__ == "__main__":
    main()
