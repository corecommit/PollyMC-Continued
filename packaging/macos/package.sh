#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 4 || $# -gt 5 ]]; then
    echo "Usage: $0 BUILD_DIR INSTALL_DIR OUTPUT_DIR VERSION [BUILD_CONFIG]" >&2
    exit 2
fi

build_dir=$1
install_dir=$2
output_dir=$3
version=$4
build_config=${5:-Release}

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source_root=$(cd "$script_dir/../.." && pwd)

case "$version" in
    ''|*[!0-9.]*) echo "VERSION must use MAJOR.MINOR.PATCH format" >&2; exit 2 ;;
esac
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "VERSION must use MAJOR.MINOR.PATCH format" >&2; exit 2; }

[[ $(uname -s) == Darwin ]] || { echo "macOS packaging must run on macOS" >&2; exit 1; }
[[ -d $build_dir ]] || { echo "Build directory not found: $build_dir" >&2; exit 1; }

for directory in "$install_dir" "$output_dir"; do
    [[ -n $directory && $directory != / ]] || { echo "Refusing unsafe output directory: $directory" >&2; exit 1; }
done

cmake -E rm -rf "$install_dir"
cmake -E make_directory "$install_dir" "$output_dir"
cmake --install "$build_dir" --config "$build_config" --prefix "$install_dir"

app="$install_dir/PollyMC.app"
[[ -d $app ]] || { echo "Installed application not found: $app" >&2; exit 1; }
app_executable="$app/Contents/MacOS/PollyMC"

# Remove plugins for unused Qt modules.
cmake -E rm -f \
    "$app/Contents/PlugIns/imageformats/libqpdf.dylib" \
    "$app/Contents/PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib"

find "$app" \( -name '.DS_Store' -o -name '._*' \) -delete
xattr -cr "$app"
chmod +x "$app_executable"

# Bundle bot-server NEXT TO the app bundle, not inside it:
# codesign fails on unsigned files inside the bundle, and
# BotProcess::findBotServerDir() resolves ../bot-server from the app dir.
# node_modules is not bundled; installed on first use from the Bot Manager.
bundle_bot_server() {
    dest=$1
    cmake -E rm -rf "$dest/bot-server"
    cp -R "$source_root/bot-server" "$dest/bot-server"
    rm -f "$dest/bot-server/.gitignore"
    rm -rf "$dest/bot-server/node_modules"
}

bundle_bot_server "$install_dir"

# Remove build-machine search paths.
while IFS= read -r -d '' candidate; do
    file "$candidate" | grep -q 'Mach-O' || continue
    while IFS= read -r rpath; do
        case "$rpath" in
            @*) ;;
            *) install_name_tool -delete_rpath "$rpath" "$candidate" ;;
        esac
    done < <(otool -l "$candidate" | awk '/cmd LC_RPATH/ { getline; getline; print $2 }')
done < <(find "$app/Contents" -type f -print0)

if [[ -n ${APPLE_CODESIGN_ID:-} ]]; then
    signing_identity=$APPLE_CODESIGN_ID
    entitlements="$source_root/program_info/App.entitlements"
    timestamp_args=(--timestamp)
else
    signing_identity=-
    entitlements="$source_root/program_info/AdhocSignedApp.entitlements"
    timestamp_args=(--timestamp=none)
fi

source_branch=$(git -C "$source_root" symbolic-ref --short -q HEAD 2>/dev/null || true)

sign_code() {
    codesign \
        --force \
        --sign "$signing_identity" \
        --options runtime \
        "${timestamp_args[@]}" \
        "$1"
}

# Sign nested code before the app bundle.
while IFS= read -r -d '' candidate; do
    file "$candidate" | grep -q 'Mach-O' || continue
    [[ $candidate == "$app_executable" ]] && continue
    [[ $candidate == *.framework/* ]] && continue
    sign_code "$candidate"
done < <(find "$app/Contents" -type f -print0)

while IFS= read -r -d '' framework; do
    sign_code "$framework"
done < <(find "$app/Contents/Frameworks" -type d -name '*.framework' -prune -print0)

codesign \
    --force \
    --sign "$signing_identity" \
    --entitlements "$entitlements" \
    --options runtime \
    "${timestamp_args[@]}" \
    "$app"

EXPECTED_VERSION=$version \
EXPECTED_ARCHS="${EXPECTED_ARCHS:-}" \
EXPECTED_MIN_MACOS="${EXPECTED_MIN_MACOS:-}" \
SOURCE_ROOT=$source_root \
SOURCE_BRANCH=$source_branch \
    "$script_dir/verify_bundle.sh" "$app"

artifact_arch=${ARTIFACT_ARCH:-$(uname -m)}
artifact_base="PollyMC-Continued-${version}-macOS-${artifact_arch}"
zip_path="$output_dir/$artifact_base.zip"
dmg_path="$output_dir/$artifact_base.dmg"

notarize_values=(
    "${APPLE_NOTARIZE_APPLE_ID:-}"
    "${APPLE_NOTARIZE_TEAM_ID:-}"
    "${APPLE_NOTARIZE_PASSWORD:-}"
)
notarize_count=0
for value in "${notarize_values[@]}"; do
    [[ -z $value ]] || notarize_count=$((notarize_count + 1))
done

if [[ $notarize_count -ne 0 && $notarize_count -ne 3 ]]; then
    echo "All notarization credentials must be provided together" >&2
    exit 1
fi

if [[ $notarize_count -eq 3 ]]; then
    notarize_zip="$output_dir/.notarize.zip"
    ditto -c -k --sequesterRsrc --keepParent "$app" "$notarize_zip"
    xcrun notarytool submit "$notarize_zip" \
        --wait \
        --apple-id "$APPLE_NOTARIZE_APPLE_ID" \
        --team-id "$APPLE_NOTARIZE_TEAM_ID" \
        --password "$APPLE_NOTARIZE_PASSWORD"
    xcrun stapler staple "$app"
    xcrun stapler validate "$app"
    cmake -E rm -f "$notarize_zip"
fi

cmake -E rm -f "$zip_path" "$dmg_path"
# ponytail: bot-server deliberately excluded from the portable zip - ditto
# rejects --keepParent with multiple sources; the dmg carries it. Add it back
# with /usr/bin/zip if the zip build needs bots.
ditto -c -k --norsrc --noextattr --noqtn --noacl --keepParent "$app" "$zip_path"

if unzip -Z1 "$zip_path" | grep -Eq '(^|/)__MACOSX/|(^|/)\._'; then
    echo "ZIP contains Finder or AppleDouble metadata" >&2
    exit 1
fi

zip_check_dir="$install_dir/zip-check"
cmake -E rm -rf "$zip_check_dir"
cmake -E make_directory "$zip_check_dir"
ditto -x -k "$zip_path" "$zip_check_dir"
EXPECTED_VERSION=$version \
EXPECTED_ARCHS="${EXPECTED_ARCHS:-}" \
EXPECTED_MIN_MACOS="${EXPECTED_MIN_MACOS:-}" \
SOURCE_ROOT=$source_root \
SOURCE_BRANCH=$source_branch \
    "$script_dir/verify_bundle.sh" "$zip_check_dir/PollyMC.app"
cmake -E rm -rf "$zip_check_dir"

dmg_root="$install_dir/dmg-root"
dmg_rw="$output_dir/$artifact_base-rw.dmg"
dmg_mount="$install_dir/dmg-mount"
dmg_check="$install_dir/dmg-check"
cmake -E rm -rf "$dmg_root"
cmake -E make_directory "$dmg_root"
ditto --norsrc --noextattr --noqtn --noacl "$app" "$dmg_root/PollyMC.app"
bundle_bot_server "$dmg_root"
ln -s /Applications "$dmg_root/Applications"
cp "$app/Contents/Resources/PollyMC.icns" "$dmg_root/.VolumeIcon.icns"

setfile=$(xcrun --find SetFile)
getfileinfo=$(xcrun --find GetFileInfo)

hdiutil create \
    -volname "PollyMC-Continued $version" \
    -srcfolder "$dmg_root" \
    -format UDRW \
    -fs APFS \
    -ov \
    "$dmg_rw"

cmake -E rm -rf "$dmg_mount"
cmake -E make_directory "$dmg_mount"
hdiutil attach -nobrowse -readwrite -mountpoint "$dmg_mount" "$dmg_rw" >/dev/null
if ! "$setfile" -a C "$dmg_mount"; then
    hdiutil detach "$dmg_mount" >/dev/null || true
    exit 1
fi
sync
hdiutil detach "$dmg_mount" >/dev/null

hdiutil convert \
    "$dmg_rw" \
    -format UDZO \
    -imagekey zlib-level=9 \
    -o "$dmg_path"
cmake -E rm -f "$dmg_rw"
hdiutil verify "$dmg_path"

cmake -E rm -rf "$dmg_check"
cmake -E make_directory "$dmg_check"
hdiutil attach -nobrowse -readonly -mountpoint "$dmg_check" "$dmg_path" >/dev/null
dmg_attributes=$("$getfileinfo" -a "$dmg_check")
if [[ $dmg_attributes != *C* || ! -s "$dmg_check/.VolumeIcon.icns" || ! -L "$dmg_check/Applications" ]]; then
    hdiutil detach "$dmg_check" >/dev/null || true
    echo "DMG layout or volume icon is invalid" >&2
    exit 1
fi
if ! EXPECTED_VERSION=$version \
    EXPECTED_ARCHS="${EXPECTED_ARCHS:-}" \
    EXPECTED_MIN_MACOS="${EXPECTED_MIN_MACOS:-}" \
    SOURCE_ROOT=$source_root \
    SOURCE_BRANCH=$source_branch \
        "$script_dir/verify_bundle.sh" "$dmg_check/PollyMC.app"; then
    hdiutil detach "$dmg_check" >/dev/null || true
    exit 1
fi
hdiutil detach "$dmg_check" >/dev/null
cmake -E rm -rf "$dmg_check"

(
    cd "$output_dir"
    shasum -a 256 "$(basename "$zip_path")" "$(basename "$dmg_path")" > "$artifact_base.sha256"
)

echo "Created $zip_path"
echo "Created $dmg_path"
