#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
	echo "Usage: $0 /path/to/PollyMC.app" >&2
	exit 2
fi

app=$1
contents="$app/Contents"
plist="$contents/Info.plist"

fail() {
	echo "macOS bundle verification failed: $*" >&2
	exit 1
}

[[ $(uname -s) == Darwin ]] || fail "this check must run on macOS"
[[ -d $app ]] || fail "application bundle not found: $app"
[[ -f $plist ]] || fail "Info.plist is missing"
plutil -lint "$plist" >/dev/null || fail "Info.plist is invalid"

executable_name=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$plist")
executable="$contents/MacOS/$executable_name"
[[ -x $executable ]] || fail "bundle executable is missing or not executable"

bundle_name=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleName' "$plist")
display_name=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleDisplayName' "$plist")
package_type=$(/usr/libexec/PlistBuddy -c 'Print :CFBundlePackageType' "$plist")
minimum_system_version=$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$plist")
[[ -n $bundle_name && $display_name == "$bundle_name" ]] || fail "bundle display name is missing or inconsistent"
[[ $package_type == APPL ]] || fail "bundle package type is not APPL"
[[ $minimum_system_version =~ ^[0-9]+(\.[0-9]+){1,2}$ ]] || fail "minimum macOS version is missing or invalid"

bundle_version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$plist")
if [[ -n ${EXPECTED_VERSION:-} && $bundle_version != "$EXPECTED_VERSION" ]]; then
	fail "bundle version is $bundle_version, expected $EXPECTED_VERSION"
fi

icon_name=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "$plist")
icon="$contents/Resources/$icon_name"
[[ -f $icon ]] || icon="$icon.icns"
[[ -s $icon ]] || fail "application icon is missing: $icon_name"

icon_check_dir=$(mktemp -d)
trap 'rm -rf "$icon_check_dir"' EXIT
iconutil -c iconset "$icon" -o "$icon_check_dir/AppIcon.iconset"
[[ -s "$icon_check_dir/AppIcon.iconset/icon_512x512@2x.png" ]] || fail "application icon has no 1024px representation"

[[ -f "$contents/PlugIns/platforms/libqcocoa.dylib" ]] || fail "Qt Cocoa platform plugin is missing"
[[ -f "$contents/PlugIns/iconengines/libqsvgicon.dylib" ]] || fail "Qt SVG icon engine is missing"
[[ -f "$contents/Frameworks/QtSvg.framework/Versions/A/QtSvg" ]] || fail "Qt SVG framework is missing"
[[ -f "$contents/Resources/qt.conf" ]] || fail "qt.conf is missing"

for jar in JavaCheck.jar NewLaunch.jar NewLaunchLegacy.jar SkinAgent.jar; do
	[[ -s "$contents/Resources/jars/$jar" ]] || fail "runtime component is missing: $jar"
done

if [[ -n ${EXPECTED_ARCHS:-} ]]; then
	actual_archs=" $(lipo -archs "$executable") "
	for expected_arch in $EXPECTED_ARCHS; do
		[[ $actual_archs == *" $expected_arch "* ]] || fail "executable does not contain $expected_arch (has:${actual_archs})"
	done
fi

executable_dir=$(dirname "$executable")
executable_rpaths=()
while IFS= read -r rpath; do
	[[ -z $rpath ]] || executable_rpaths+=("$rpath")
done < <(otool -l "$executable" | awk '/cmd LC_RPATH/ { getline; getline; print $2 }')

expand_runtime_path() {
	local runtime_path=$1
	local loader_dir=$2
	runtime_path=${runtime_path//@executable_path/$executable_dir}
	runtime_path=${runtime_path//@loader_path/$loader_dir}
	printf '%s\n' "$runtime_path"
}

version_is_at_most() {
	awk -v actual="$1" -v maximum="$2" 'BEGIN {
        actual_count = split(actual, actual_parts, ".")
        maximum_count = split(maximum, maximum_parts, ".")
        count = actual_count > maximum_count ? actual_count : maximum_count
        for (i = 1; i <= count; i++) {
            a = actual_parts[i] + 0
            b = maximum_parts[i] + 0
            if (a < b) exit 0
            if (a > b) exit 1
        }
        exit 0
    }'
}

if [[ -n ${EXPECTED_MIN_MACOS:-} && $minimum_system_version != "$EXPECTED_MIN_MACOS" ]]; then
	fail "Info.plist requires macOS $minimum_system_version, expected $EXPECTED_MIN_MACOS"
fi

dependency_errors=0
while IFS= read -r -d '' candidate; do
	file "$candidate" | grep -q 'Mach-O' || continue
	install_name=$(otool -D "$candidate" 2>/dev/null | awk 'NR == 2 { print $1 }')
	loader_dir=$(dirname "$candidate")
	candidate_rpaths=()

	if [[ -n ${EXPECTED_ARCHS:-} ]]; then
		candidate_archs=" $(lipo -archs "$candidate") "
		for expected_arch in $EXPECTED_ARCHS; do
			if [[ $candidate_archs != *" $expected_arch "* ]]; then
				echo "Missing architecture $expected_arch in $candidate (has:${candidate_archs})" >&2
				dependency_errors=1
			fi
		done
	fi

	if [[ -n ${EXPECTED_MIN_MACOS:-} ]]; then
		while IFS= read -r minimum_macos; do
			if ! version_is_at_most "$minimum_macos" "$EXPECTED_MIN_MACOS"; then
				echo "$candidate requires macOS $minimum_macos (maximum allowed is $EXPECTED_MIN_MACOS)" >&2
				dependency_errors=1
			fi
		done < <(vtool -show-build "$candidate" 2>/dev/null | awk '$1 == "minos" { print $2 }')
	fi

	while IFS= read -r rpath; do
		[[ -z $rpath ]] && continue
		candidate_rpaths+=("$rpath")
		case "$rpath" in
		@*) ;;
		*)
			echo "External rpath in $candidate: $rpath" >&2
			dependency_errors=1
			;;
		esac
	done < <(otool -l "$candidate" | awk '/cmd LC_RPATH/ { getline; getline; print $2 }')

	while IFS= read -r dependency; do
		[[ -n $install_name && $dependency == "$install_name" ]] && continue
		case "$dependency" in
		@executable_path/* | @loader_path/*)
			resolved_dependency=$(expand_runtime_path "$dependency" "$loader_dir")
			if [[ ! -e $resolved_dependency ]]; then
				echo "Unresolved dependency in $candidate: $dependency" >&2
				dependency_errors=1
			fi
			;;
		@rpath/*)
			dependency_suffix=${dependency#@rpath/}
			dependency_found=0
			for rpath in "${candidate_rpaths[@]}" "${executable_rpaths[@]}"; do
				[[ -z $rpath ]] && continue
				expanded_rpath=$(expand_runtime_path "$rpath" "$loader_dir")
				if [[ -e "$expanded_rpath/$dependency_suffix" ]]; then
					dependency_found=1
					break
				fi
			done
			if [[ $dependency_found -eq 0 ]] && [[ -e "$contents/Frameworks/$dependency_suffix" ]]; then
				dependency_found=1
			fi
			if [[ $dependency_found -eq 0 ]]; then
				echo "Unresolved dependency in $candidate: $dependency" >&2
				dependency_errors=1
			fi
			;;
		/System/Library/* | /usr/lib/*) ;;
		"$app"/*)
			# ponytail: Qt 6.11 macdeployqt may leave framework install names pointing into the bundle.
			# The dependency is inside the bundle, so it's not external.
			;;
		@*)
			echo "Unsupported loader token in $candidate: $dependency" >&2
			dependency_errors=1
			;;
		*)
			echo "External dependency in $candidate: $dependency" >&2
			dependency_errors=1
			;;
		esac
	done < <(otool -L "$candidate" | awk 'NR > 1 { print $1 }')
done < <(find "$contents" -type f -print0)
[[ $dependency_errors -eq 0 ]] || fail "bundle contains dependencies outside the application or macOS"

while IFS= read -r -d '' symlink; do
	[[ -e $symlink ]] || fail "bundle contains a broken symlink: $symlink"
done < <(find "$app" -type l -print0)

for private_path in "${SOURCE_ROOT:-}" "${HOME:-}"; do
	[[ -n $private_path && $private_path != / ]] || continue
	# The bundled libonnxruntime dylib is exempt: Microsoft builds it on
	# GitHub Actions runners, so it legitimately contains /Users/runner
	# build paths of its own. Nothing of ours can hide in a file we
	# download instead of compile.
	if LC_ALL=C grep -R -a -F -q --exclude='libonnxruntime.*.dylib' -- "$private_path" "$app"; then
		fail "bundle contains a private build path"
	fi
done

# stable is the release marker written by Launcher_BUILD_RELEASE, so it can't be told apart from a real branch.
case ${SOURCE_BRANCH:-} in
'' | main | master | develop | stable) ;;
*)
	# Binaries like QtWidgets/libzstd contain 'testing' by chance; only scan text, plus git refspec in binaries.
	if LC_ALL=C grep -R --binary-files=without-match -q -F -- "$SOURCE_BRANCH" "$app" ||
		LC_ALL=C grep -R -a -q -F -- "refs/heads/$SOURCE_BRANCH" "$app"; then
		fail "bundle contains the local source branch name"
	fi
	;;
esac

if find "$app" \( -name '.DS_Store' -o -name '._*' \) -print -quit | grep -q .; then
	fail "bundle contains Finder or AppleDouble metadata"
fi

if find "$app" \
	\( -iname '*.log' \
	-o -iname 'accounts.json' \
	-o -iname 'launcher_accounts.json' \
	-o -iname 'pollymc.cfg' \
	-o -iname 'prismlauncher.cfg' \
	-o -iname 'instance.cfg' \
	-o -type d -iname 'logs' \
	-o -type d -iname 'instances' \
	-o -type d -iname 'crash-reports' \) \
	-print -quit | grep -q .; then
	fail "bundle contains local launcher data or logs"
fi

codesign --verify --deep --strict --verbose=2 "$app"
echo "Verified portable macOS bundle: $app ($bundle_version)"
