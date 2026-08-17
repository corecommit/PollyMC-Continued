# Changelog

## v9.2.7

**Added:**
- Linux binary tarball release artifact (`PollyMC-Continued-*-Linux-x86_64.tar.gz`) — portable install with the same layout as the Windows portable zip
- Finnish, Hungarian, Japanese, and Laotian translations completed to 100%

**Changed:**
- Release `.deb` and `.pkg.tar.zst` artifacts renamed to the `PollyMC-Continued-*` naming scheme (package install names unchanged)
- Mods page table columns now resize proportionally with the window — Name, Last Modified, and Provider stretch; the Enable checkbox column sizes to its content
- Dropped the unused pacman database files (`pollymc-continued.db`/`.files`) from build artifacts
- Mod folder parsing (zip handling, metadata, icon decoding) now runs off the UI thread, so loading large mod folders no longer freezes the launcher
- Column-layout saves and the resource browser search are now debounced instead of firing on every pixel of a drag or every keystroke
- Bot system no longer bundles Node.js modules into installs — they are downloaded on first use
- Bot Manager now checks for Node.js and the required modules when opened, prompting the user to install them when missing

**Fixed:**
- Launcher ignored display scaling at 125% and 150% on Windows and macOS (the rounding policy floored fractional scales to 100%); the policy now only applies on Linux
- Closing the Java wizard after enabling automatic Java download crashed the launcher (double-free of a layout item)
- Jar mods were leaked on every launch (`getJarMods()` returned unowned objects)
- Setup wizard, category filter checkboxes, and in-flight icon downloads leaked when dismissed or closed
- Mod dependency relationships rebuilt in O(n²) time on large mod folders — now uses lookup tables
- Resolving the cache could stall the UI with a full-file MD5 read and unbounded `xdg-mime` wait
- Bot dependency install could hang forever if npm failed to start; the bot status now shows the install progress
- Launcher shutdown no longer blocks on draining the global thread pool

## v9.2.6

**Added:**
- Debian/Ubuntu apt repository — `.deb` packages installable via `sudo apt install pollymc-continued`, hosted on GitHub Pages (optionally GPG-signed)
- Arch Linux pacman repository — `.pkg.tar.zst` packages installable via `sudo pacman -S pollymc-continued`, hosted on GitHub Pages
- `deploy-repos.yml` workflow publishing both repositories to GitHub Pages on every release
- README "Install" section covering both package managers

**Changed:**
- Releases now include `.deb` and `.pkg.tar.zst` packages
- Account page button now reads "Add &amp;Authlib-injector"
- Translations now load from the `translations` subdirectory of our GitHub Pages site

**Fixed:**
- Afrikaans translation now 100% complete

**Removed:**
- Joke languages (`en@pirate`, `en@uwu`, `grc`, `jam`, `peo`, `rue`) and their translation files

## v9.2.5

**Fixed:**
- Bundle Qt image format plugins in Linux AppImage
- Repair malformed plural entries in all translations so every language compiles again
- Custom Skin not loading on singleplayer world
- Cache skins resolved

## v9.2.4

**Added:**
- Linux man page (`pollymc.6`) for the launcher
- Installer now asks before creating a desktop shortcut
- Silent installs skip the desktop shortcut prompt automatically and don't touch the desktop
- Translation pipeline restored: `.ts` files are compiled to `.qm` and published to our own GitHub Pages
- Translations now load from our GitHub Pages instead of Prism's i18n server
- "Help us with translations!" link now points to our repository
- CONTRIBUTING.md with the translation contribution flow (fork, edit `.ts`, open a PR)

**Fixed:**
- NeoForge 1.21.x (and modern Forge) instances no longer crash on launch when using an offline account: the skin agent no longer claims the one-time slot — it now registers a protocol handler leaving the slot free for NeoForge/Quilt
- Auto-updater now detects new releases published under the same version number
- Installer and uninstaller now use the PollyMC icon instead of the NSIS default

## v9.2.3

**Added:**
- More bugs to fix later. You're welcome. (It's this changelog entry, but let's be honest: it reads as a feature at this point)

**Fixed:**
- Language selection now lists all available translations instead of only English
- "Help us with translations!" link now points to [NEEDS UPDATE — Weblate reference is stale, tell me the correct current target]
- Minecraft 1.8.9 and other Java 8 instances no longer fail to start when using an offline account: the skin agent's module options, which only Java 9+ understands, are no longer passed to Java 8
- Updates are now only offered for actual installable files: checksum and signature files attached to releases (e.g. `.sha256`, `.asc`) can no longer be mistaken for an update
- Auto-updater no longer leaves a full copy of the update inside the install folder: extraction now happens in the system temp folder, and extraction leftovers from older updates are removed when the update installs
- Portable installs now reliably receive files that are new to a release: the updater installs the file list shipped with the update instead of guessing from the old install folder
- Help buttons that previously opened a dead link (and did nothing at all) now say what's what instead of silently failing — pending documentation, they politely tell you the page is "coming later"

**Removed:**
- Cat feature: "Meow" button (toolbar and View menu), cat packs folder entry, cat background/overlay rendering, cat pack selector and cat scaling/opacity settings in Appearance, the CatPack/CatPainter classes and all cat icon assets, and "And cat :3" from the Linux AppStream metadata

## v9.2.2

**Changed:**
- Linux AppImage bundles the KDE Breeze widget style
- Fractional screen scaling rounds sanely under KDE Plasma; per-screen scale factors logged at startup
- Java diagnostics redact secret-looking environment values (TOKEN/KEY/SECRET/PASSWORD/AUTH)

**Fixed:**
- Portable Windows zip now includes `portable.txt`, so the launcher and auto-updater detect and update portable installs correctly
- Windows installer asset renamed to include "Windows" (e.g. `PollyMC-Continued-9.2.2-Windows-Setup.exe`) so the auto-updater can match it

## v9.2.1

**Changed:**
- Bot Manager: commands that act on a bot no longer take a username — you must select the bot(s) in the table first; multi-selected bots are targeted together, and `/quit` now disconnects the selected bot(s) instead of `<username>`

**Fixed:**
- Quilt instances no longer fail to launch when using an offline account
- "Not enough RAM" warning now compares the allocation against total installed RAM instead of momentary free RAM
- Updater no longer reports a new version when the installed version matches the latest release (macOS builds now carry a build number distinct from the marketing version)

## v9.2.0

**Added:**
- Bot system: manage Minecraft bots in the new Bot Manager window (Help menu) — add, edit, remove, start and stop bots, multi-select rows
- Bot Manager: console commands `/join <server> [username] [port]`, `/quit`, `/list`, `/say <message>`, `/follow <player>`, `/stop`, `/goto <x> <y> <z>`, `/home`, `/pos`, `/health`, `/inventory`, `/drop <item> [count]`, `/equip <item>`, `/whisper <player> <message>`, `/respawn`, `/players`, `/help`, `/clear` (clears the console)
- Bot Manager: Commands button showing every command with its usage
- Bot Manager: slash-command autocomplete popup — type `/` in the console to see available commands
- Bot Manager: per-bot Minecraft version picker, actually used when the bot connects
- System tray icon with Show/Quit menu — "Minimize to Tray" no longer makes the app unreachable

**Changed:**
- Bot Manager: Send button now inherits the launcher theme instead of a hardcoded green

**Removed:**
- Bot Manager: "Microsoft account" login option (not implemented)
- Dead BotTerminalPage code

**Fixed:**
- Auto-updater: release list fetch blocked by a malformed repo URL
- Updater layout: restore checkboxes no longer overlap the interval spinner
- Discord Rich Presence: IPC probing no longer freezes the UI
- Plaintext account password no longer written to `accounts.json` on hard auth failure
- Auth server URL validated in login dialog (auto-prepends `https://`, rejects non-http schemes)
- Yggdrasil skin model read from texture metadata ("slim"/"classic") instead of hardcoded slim
- Multi-profile picker now has a proper parent window
- Bot Manager: opening it twice no longer spawns duplicate bot servers
- Bot Manager: Stop actually disconnects bots
- Bot Manager: Add/Edit dialog can't accept an empty bot name (cancelled vs empty now unambiguous)
- Bot Manager: `/join` supports a port argument
- Bot Manager: duplicate bot names rejected

## v9.1.1

**Added:**
- Detect auth server downtime and launch offline with clearer dialog ("Auth servers offline" vs "No internet connection")
- Retain network error info across auth steps for better offline fallback messaging
- Skip pointless retries on HTTP 404 responses in NetJob

**Changed:**
- Update README: remove downloads section, add contributor avatars
- Add Discord badge and enable Discord Guild menu item in launcher
- Installer now uses CI version instead of hardcoded "1.0.0" for registry DisplayVersion
- Enable auto-updater (Windows/Linux via GitHub releases, macOS via Sparkle)

**Fixed:**
- Memory leak and potential crash in data packs modal dialog (non-modal with WA_DeleteOnClose)
- Infinite update check loop when timer fires during an ongoing check (re-entrancy guard)
- Updater now builds and runs on all platforms (removed `NOT APPLE` guard); macOS falls back to PrismExternalUpdater when Sparkle feed URL is empty

## v9.1.0

**Added:**
- Yggdrasil (authlib-injector) account support
  - New AccountType::AuthlibInjector
  - AuthlibInjectorStep for /authserver/authenticate and /authserver/refresh
  - Login dialog with server URL, username, password
  - JVM agent injection for authlib-injector at launch
  - "Add Yggdrasil" toolbar button
  - Skin fetching via sessionserver profile endpoint
  - Multi-profile picker when server returns multiple availableProfiles
- macOS derives version from git tag (like Windows/Linux)

**Changed:**
- Add ccache to CI builds (Windows, Linux, macOS) for faster rebuilds

**Removed:**
- Unused accountIsOnline variable

**Fixed:**
- Cancel button not working in Yggdrasil login dialog
- Yggdrasil accounts not fetching player skins
- Old PollyMC accounts with missing auth server URL now show clear error
- PollyMC ↔ PollyMC-Continued account compatibility (reads both `authlibInjectorUrl` and `auth-server-url`, writes `authlibInjectorUrl`)
- Use accountData() accessor instead of protected data member
- authlib-injector.jar not found on AppImage (`applicationDirPath()` resolves to temp mount; added `dataRoot()/libraries/` as persistent fallback)

## v9.0.9

**Added:**
- Portable macOS build and DMG
- Reliable local macOS build script
- Local macOS build setup hardening
- qtwebsockets module to build workflow
- Qt 6.11.* support (bump from 6.9.3)
- macOS deployment target bumped to 13.0
- Contributors section in README
- Branch name check skips main/master/develop

**Changed:**
- Revise README for clarity and feature highlights
- Shorten README

**Fixed:**
- Use --codesigning=off for Qt 6.9+ macdeployqt
- Remove codesign flags for Qt 6.9+ macdeployqt
- verify_bundle.sh for Qt 6.11: handle @rpath/Frameworks and bundle-internal framework deps
- Skip binary files in branch name grep to avoid matching 'main' in compiled code

## v9.0.8

**Added:**
- Dll Checks in Github Action
- Enhance build script with DLL checks and updates
- Enhance release workflow with additional triggers
- Enhance versioning logic in build.yml

**Changed:**
- Refactor CI workflow for Windows and Linux builds
- Refactor DLL dependency handling in build workflow
- Refactor CMake build process and improve logging
- Refactor deployment script for improved clarity
- Update build permissions and fix version handling
- Update release workflow to handle versioning
- Improve DLL deployment logic

**Fixed:**
- Filter Windows system DLLs from recursive dep check; add installer DLL test step
- Add msvcp_win.dll to Windows system DLL skip list
- Replace libgamemode-dev with gamemode-dev for Ubuntu 24.04
- Cache linuxdeploy step with proper key and restore-keys
- Formatting and error messages in build.yml
- Formatting issue in build.yml
- GitHub actions workflow bugs (×2)
- Icon naming in .desktop file

## v9.0.7

**Added:**
- Bundle OpenSSL into AppImage

**Fixed:**
- NSIS script and DLL copy locations

## v9.0.6

**Fixed:**
- Reduce parallel jobs to -j2 for Windows build

## v9.0.5

**Fixed:**
- NSIS installer output directory bug

## v9.0.4

**Fixed:**
- Limit parallel jobs in build step

## v9.0.3

**Fixed:**
- Add error checking to Windows build steps

## v9.0.2

**Other:**
- Debug: Add more file verification steps

## v9.0.1

**Changed:**
- Update CI workflow
- Update pollymc icon, remove old files

**Fixed:**
- Deploy MinGW runtime DLLs in Windows build
- Resolve release conflict in CI workflow
- Let GitHub auto-generate release notes from commits

**Other:**
- Debug: Verify release files exist before upload
