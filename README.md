# PollyMC-Continued

A revival of [PollyMC](https://github.com/fn2006/PollyMC), a free and open-source Minecraft launcher based on [Prism Launcher](https://prismlauncher.org).

## What's Different

PollyMC-Continued lets you play Minecraft **without needing a Microsoft account**. You can add offline accounts directly and launch the full game with no restrictions.

### Features
- **Offline accounts** — add and play without Microsoft login
- **No demo mode** — offline accounts launch the full game
- **Custom skins** — save and load skins locally for offline accounts
- **Offline skin agent** — Java agent that intercepts skin API to serve local skins
- **Setup wizard** — offers offline account option on first launch
- **NSIS installer** — one-click install with Start Menu/Desktop shortcuts
- **Upgrade support** — detects existing installation and offers to upgrade

## Download

Grab the latest release from the [Releases](https://github.com/SharathGames1/PollyMC/releases) page.

## Building

### Prerequisites (Windows)
- **MSYS2** with MinGW-w64
- **Qt 6** (`mingw-w64-x86_64-qt6-base`, `mingw-w64-x86_64-qt6-svg`, etc.)
- **CMake** + **Ninja**
- **Java JDK** (for building launcher JARs)
- **NSIS** (for creating the setup installer)

### Build Steps
```bash
# Configure with MinGW runtime paths
cmake -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/c/msys64/mingw64 \
  -DCMAKE_SYSTEM_LIBRARY_PATH=/c/msys64/mingw64/bin \
  -DCMAKE_CXX_FLAGS="-Wno-error" \
  -DCMAKE_C_FLAGS="-Wno-error" \
  .

# Build
cmake --build . -j4 --target PollyMC

# Install to build directory (deploys Qt + MinGW runtime DLLs)
cmake --install . --prefix C:/pollymc_build

# Create setup installer
"/c/Program Files (x86)/NSIS/makensis.exe" pollymc_installer.nsi
```

## Credits

- [Prism Launcher](https://github.com/PrismLauncher/PrismLauncher) — the base launcher
- [PolyMC](https://github.com/PolyMC/PolyMC) — original fork
- [MultiMC](https://multimc.org) — the original launcher

## License

GPL-3.0. See [LICENSE](LICENSE) for details.
