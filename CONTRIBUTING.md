# Contributing to PollyMC-Continued

Thanks for considering contributing! This project is a fork of Prism Launcher, so most of the general contribution guidelines from [Prism's contributing guide](https://github.com/PrismLauncher/PrismLauncher/blob/develop/CONTRIBUTING.md) apply here as well.

## Translating

Translations live in the [`translations/`](translations/) directory as Qt Linguist `.ts` files — one file per language (e.g. `de.ts`, `fr.ts`, `pt_BR.ts`).

### How to translate

1. **Fork** this repository on GitHub.
2. **Edit** the `.ts` file for your language. The easiest way is to open it with [Qt Linguist](https://doc.qt.io/qt-6/qtlinguist-index.html), but editing the XML by hand works too.
   - Don't have a `.ts` file for your language? Use `lupdate` on the source tree, copy an existing `.ts` file and change the `language` attribute, or open an issue asking for one.
3. **Commit** your changes and **open a pull request** against `main`.
4. That's it. Once the PR is merged, the translation files are compiled to `.qm` and published automatically, and the launcher will pick them up at the next refresh.

### Tips

- Only translate the `<translation>` contents — never change `<source>` or the message context.
- Leave untranslated strings empty or as-is; the launcher falls back to English.
- If a string is marked `<type>unfinished</type>`, it's not yet considered translated.
- Don't translate launcher-specific tokens like `%1` — keep them in the translation.

Everything else (bug reports, feature requests, code contributions) goes through the normal pull request flow.