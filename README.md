# PollyMC-Continued

> **Heads up:** This is a revival of PollyMC — forked from [Prism Launcher](https://github.com/PrismLauncher/PrismLauncher), not from fn2006's original repo.
>
> [![Discord](https://img.shields.io/badge/Discord-5865F2?style=for-the-badge&logo=discord&logoColor=white)](https://discord.gg/FsM3JNTN9z)

Lets you play Minecraft **without a Microsoft account** — add offline accounts and launch the full game with no restrictions.

## Features

- **Offline accounts** — no Microsoft login required; offline accounts launch the full game
- **Authlib-injector** account support for third-party auth servers
- **Skins** — local skin library, offline skin agent that serves skins to the game, and an online skin browser (crafty.gg) with search, page navigation, and a rotatable 3D preview
- **Command palette** — Ctrl+Shift+P opens a searchable list of every launcher action; navigate with arrows, run with Enter
- **Bot manager** — add Minecraft bots, connect them to a server, control them from a built-in console, and script sequences of actions (chat, commands, waits, loops)
- **Setup wizard** — offers offline account on first launch
- **Automatic updater** — GitHub releases on Windows and Linux
- **NSIS installer** with upgrade support
- **Package repositories** — apt and pacman, published via GitHub Pages

## Install

Download the artifact for your platform from the [releases page](https://github.com/PollyMC-Continued/launcher/releases).

### Windows

- **Installer** — `PollyMC-Continued-9.3.0-Windows-Setup.exe`. Runs the NSIS installer, creates a Start Menu entry, supports upgrade-in-place.
- **Portable** — `PollyMC-Continued-9.3.0-Windows-portable.zip`. Extract anywhere, run `pollymc.exe`. Data lives next to the binary.

### macOS (arm64)

- **Disk image** — `PollyMC-Continued-9.3.0-macOS-arm64.dmg`. Open, drag `PollyMC.app` to Applications.
- **ZIP** — same contents, for scripted installs.

Auto-update is not available on macOS yet — download the latest `.dmg` or `.zip` when a new release is out.

### Linux

- **AppImage** — `PollyMC-Continued-9.3.0-x86_64.AppImage`. `chmod +x` and run. Self-contained; needs glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Fedora 40+, Arch).
- **Portable tarball** — `PollyMC-Continued-9.3.0-Linux-x86_64.tar.gz`. Extract, run `bin/pollymc`. Data lives in the extracted folder. Needs glibc 2.39 or newer plus a system Qt 6.4+, because it is built on Ubuntu 24.04.
- **DEB** — see the apt repository below, or install a downloaded `.deb` with `sudo dpkg -i`.
- **pacman** — see the Arch repository below, or install a downloaded `.pkg.tar.zst` with `sudo pacman -U`.

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
