# PollyMC-Continued

> **Heads up:** This is a revival of PollyMC — forked from [Prism Launcher](https://github.com/PrismLauncher/PrismLauncher), not from fn2006's original repo. I had to change and tweak everything from scratch.
>
> [![Discord](https://img.shields.io/badge/Discord-5865F2?style=for-the-badge&logo=discord&logoColor=white)](https://discord.gg/FsM3JNTN9z)

Lets you play Minecraft **without a Microsoft account** — add offline accounts and launch the full game with no restrictions.

## Features
- Offline accounts — no Microsoft login required
- No demo mode — offline accounts launch the full game
- Custom skins — save and load skins locally
- Offline skin agent — Java agent that serves local skins
- Setup wizard — offers offline account on first launch
- NSIS installer with upgrade support

## Install

### Debian / Ubuntu (apt)

Add the repository, then install with `sudo apt install`:

```bash
# (Optional) trust the repo signing key if the repository is signed
curl -fsSL https://corecommit.github.io/PollyMC-Continued/apt/pollymc-continued.gpg \
  | sudo tee /etc/apt/keyrings/pollymc-continued.asc

echo "deb [signed-by=/etc/apt/keyrings/pollymc-continued.asc] https://corecommit.github.io/PollyMC-Continued/apt stable main" \
  | sudo tee /etc/apt/sources.list.d/pollymc-continued.list

sudo apt update
sudo apt install pollymc-continued
```

If the repository is published unsigned (no `APT_SIGNING_KEY` secret configured), drop the `signed-by` option and use `[trusted=yes]` instead:

```bash
echo "deb [trusted=yes] https://corecommit.github.io/PollyMC-Continued/apt stable main" \
  | sudo tee /etc/apt/sources.list.d/pollymc-continued.list
sudo apt update && sudo apt install pollymc-continued
```

### Arch Linux (pacman)

Add the custom repository to `/etc/pacman.conf` and install with `sudo pacman -S`:

```ini
[pollymc-continued]
Server = https://corecommit.github.io/PollyMC-Continued/arch/$arch
SigLevel = Optional
```

```bash
sudo pacman -Syy
sudo pacman -S pollymc-continued
```

A `PKGBUILD` is also available in [`packaging/arch/`](packaging/arch/PKGBUILD) — it can be built locally with `makepkg` or submitted to the AUR for `yay -S` / `paru -S` installs.

## Build

### Windows
```
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/c/msys64/mingw64 -S . -B build
cmake --build build -j4
cmake --install build --prefix C:/pollymc_build
makensis pollymc_installer.nsi
```
Requires: MSYS2 + MinGW-w64, Qt 6, CMake, Ninja, NSIS.

### Linux
```
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -S . -B build
cmake --build build -j$(nproc)
cmake --install build --prefix /tmp/pollymc
# Package as AppImage (optional)
./linuxdeploy-x86_64.AppImage --appdir appdir --plugin qt --output appimage
```
Requires: Qt 6, CMake, Ninja, extra-cmake-modules, tomlplusplus, cmark, qrencode, libarchive.

### macOS
```
brew install cmake ninja qt extra-cmake-modules cmark qrencode libarchive tomlplusplus
brew install --cask temurin@17
bash packaging/macos/build-local.sh
```
Output in `dist-macos/` — ready-to-run `.app`, ZIP, DMG.

## Commit conventions

The CI auto-creates a release on every push to `main`. Which part of the version bumps depends on commit messages since the last tag:

| Commit contains | Bump | Example |
|---|---|---|
| `BREAKING CHANGE` anywhere in the message | `9.0.0` → `10.0.0` | `feat: new API`<br>`BREAKING CHANGE: removes old config format` |
| Subject starts with `feat`, `feature`, `Add`, `Build`, or `Enhance` (followed by `:`, `(`, or a space) | `9.0.0` → `9.1.0` | `feat: add drag-and-drop modpack import` |
| Anything else — e.g. `Fix:`, `Docs:`, `Chore:`, or no prefix at all | `9.0.0` → `9.0.1` | `Fix: crash when loading offline skins` |

**Notes:**
- Only the *highest* applicable bump is used — a `BREAKING CHANGE` always wins over a `feat:`, even in the same push.
- Matching is case-sensitive: `feat:` / `feature:` must be lowercase, while `Add:` / `Build:` / `Enhance:` must be capitalized as shown.
- The check looks at every commit since the last tag, not just the most recent one — so one `feat:` commit buried in a batch of `Fix:` commits still triggers a minor bump.

**Example:** if the last release was `9.0.0` and your push includes:
```
Fix: correct offline skin cache path
feat: add custom skin import from URL
Docs: update build instructions
```
The `feat:` commit triggers a **minor** bump → next release is `9.1.0`.

Put `[skip-all]` anywhere in a commit message to skip all builds and release entirely.  
Put `[skip release]` to skip only the release (builds still run, no tag created).

## Contributors

<a href="https://github.com/corecommit"><img src="https://avatars.githubusercontent.com/corecommit?s=80" width="40" height="40" alt="corecommit"/></a>
<a href="https://github.com/kiwiaraga2000"><img src="https://avatars.githubusercontent.com/kiwiaraga2000?s=80" width="40" height="40" alt="kiwiaraga2000"/></a>
<a href="https://github.com/fuis18"><img src="https://avatars.githubusercontent.com/fuis18?s=80" width="40" height="40" alt="fuis18"/></a>

## Credits
[Prism Launcher](https://github.com/PrismLauncher/PrismLauncher) · [PolyMC](https://github.com/PolyMC/PolyMC) · [MultiMC](https://multimc.org)

## License
GPL-3.0. See [LICENSE](LICENSE).
