# Contributing to PollyMC-Continued

Thanks for considering contributing! PollyMC-Continued is a fork of Prism Launcher, so the general structure and much of the architecture follows [Prism's contributing guide](https://github.com/PrismLauncher/PrismLauncher/blob/develop/CONTRIBUTING.md). This guide covers the fork-specific bits on top.

## Code of Conduct

By contributing you agree to behave respectfully and constructively. Harassment, trolling, and personal attacks are not tolerated. Maintainers may remove or reject contributions that violate this.

## Repository layout (short version)

- `launcher/` — the C++/Qt launcher application (Qt6 Widgets, C++20).
- `launcher/bot/` + `bot-server/` — the Minecraft bot manager. `bot-server/` is a Node.js process using `mineflayer`; the launcher spawns it and talks to it over newline-delimited JSON on stdin/stdout.
- `libraries/launcher/` — the Java launcher part (`org.pollymc.*`) compiled into `NewLaunch.jar`, `NewLaunchLegacy.jar` (legacy MC < 1.6) and `SkinAgent.jar` (offline skin agent). If you add or remove a `.java` file here, **update `libraries/launcher/CMakeLists.txt`** (the `SRC`/`LEGACY_SRC`/`SKIN_AGENT_SRC` lists) — it is not globbed.
- `program_info/` — branding, icons, desktop/metainfo files.
- `translations/` — Qt Linguist `.ts` files, one per language.
- `buildconfig/`, `cmake/` — build infrastructure.

## Building

Requirements: CMake >= 3.25, a C++20 compiler, Qt 6.4+ (Core, CoreTools, Widgets, Concurrent, Network, Test, Xml, NetworkAuth, OpenGL, Svg, WebSockets), and a JDK (1.7+ API, targets Java 8) for the Java launcher parts. Dependencies on top of Qt come from vcpkg (libarchive, tomlplusplus, cmark, libqrencode, zlib).

Quick start with a preset:

```bash
cmake --preset linux
cmake --build --preset linux
```

Presets live in `CMakePresets.json` (`linux`, `macos`, `macos_universal`, `windows_mingw`, `windows_msvc`). Out-of-source builds are enforced. On macOS/Windows-MSVC the preset wires up the vcpkg toolchain automatically; on Linux/BSD you can supply the system Qt and vcpkg deps yourself.

The `bot-server` needs `node` on `PATH` at runtime — the launcher only uses it if you open the Bots window.

## Code style

- **C++20**, Qt 6 **Widgets only** — there is no QML in this project; keep it that way.
- Async work goes through the `Task` framework in `launcher/tasks/` (`Task`, `SequentialTask`, `ConcurrentTask`), and network work through `NetJob`/`Download` in `launcher/net/`.
- Qt conventions: `Q_OBJECT`, `signals:`/`slots:`, `connect()` with member-function pointers.
- Every new source file starts with the SPDX + copyright header, matching the existing files:

```
// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PollyMC-Continued - Minecraft Launcher
 *  Copyright (C) <year> <Your Name> <your@email>
 *
 *  ...
 */
```

- The Java launcher (`libraries/launcher/`) uses package `org.pollymc.*`, is compiled with `-target 8 -source 8`, and its JAR entry point must stay `org.pollymc.EntryPoint` (referenced from `launcher/minecraft/launch/LauncherPartLaunch.cpp`).
- Keep user-visible strings in `tr("...")` so they can be translated.

## Commit conventions

CI derives the release version from commit messages since the last tag. Follow these rules so version bumps and releases behave:

| Commit contains | Bump | Example |
|---|---|---|
| `BREAKING CHANGE` anywhere in the message | major (`9.0.0` → `10.0.0`) | `feat: new API`<br>`BREAKING CHANGE: removes old config format` |
| Subject starts with `feat`, `feature`, `Add`, `Build`, or `Enhance` (followed by `:`, `(`, or a space) | minor (`9.0.0` → `9.1.0`) | `feat: add drag-and-drop modpack import` |
| Anything else — `Fix:`, `Docs:`, `Chore:`, or no prefix | patch (`9.0.0` → `9.0.1`) | `Fix: crash when loading offline skins` |

Notes:

- Only the **highest** applicable bump wins; `BREAKING CHANGE` always beats `feat:`.
- Matching is case-sensitive: `feat:`/`feature:` lowercase, `Add:`/`Build:`/`Enhance:` capitalized.
- The check scans **every** commit since the last tag, not just the most recent.
- Put `[skip-all]` in a commit message to skip builds and release entirely.
- Put `[skip release]` to skip only the release (builds still run).
- Put `[skip changelog]` if you are not updating `CHANGELOG.md` — otherwise CI's `changelog-check` job requires a `## vX.Y.Z` section matching the computed version.

## CHANGELOG

Add an entry under a `## vX.Y.Z` heading (create it if it doesn't exist) for user-visible changes, or use `[skip changelog]`. The entry is used as the GitHub Release notes.

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

## Testing

Run the test suite with CTest (`-DBUILD_TESTING=ON`, then `ctest`). For UI/launch changes, build the relevant preset and smoke-test locally:

- Create an offline account and an instance, then launch it.
- If you touched the bot feature, verify a bot can join a server and the slash commands respond.
- If you touched the Java launcher or skin agent, verify an offline account launches with the skin applied.

## Pull requests

- Target `main`.
- One logical change per PR; keep it focused and rebased on the latest `main`.
- Update `CHANGELOG.md` (or add `[skip changelog]`), and mention any new build/runtime dependencies.
- CI will build Windows, Linux and macOS artifacts and auto-publish a pre-release; push with `[skip-all]` if you only want to update docs/CI without building.