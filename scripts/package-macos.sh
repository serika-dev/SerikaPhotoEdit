#!/usr/bin/env sh
# Requires a completed native macOS build and the matching Qt desktop kit.
# Optional: CODESIGN_IDENTITY='Developer ID Application: ...' and NOTARY_PROFILE
# (a profile previously created with `xcrun notarytool store-credentials`).
# ICON_PNG may point to an original square PNG of at least 1024x1024 pixels.
# Native macOS execution, signing and notarization have not been verified on Windows.
set -eu
[ "$(uname -s)" = Darwin ] || { echo 'Run this packaging script on macOS.' >&2; exit 1; }
workspace=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${BUILD_DIR:-"$workspace/build"}
output_dir=${OUTPUT_DIR:-"$workspace/dist"}
case "$build_dir" in /*) ;; *) build_dir="$workspace/$build_dir" ;; esac
case "$output_dir" in /*) ;; *) output_dir="$workspace/$output_dir" ;; esac
if [ -n "${NOTARY_PROFILE:-}" ] && [ -z "${CODESIGN_IDENTITY:-}" ]; then
    echo 'NOTARY_PROFILE also requires CODESIGN_IDENTITY for a Developer ID signature.' >&2
    exit 1
fi
if [ -n "${QT_ROOT_DIR:-}" ] && [ -x "$QT_ROOT_DIR/bin/macdeployqt" ]; then
    deploy_tool="$QT_ROOT_DIR/bin/macdeployqt"
else
    deploy_tool=$(command -v macdeployqt) || { echo 'Set QT_ROOT_DIR or add the matching Qt macdeployqt to PATH.' >&2; exit 1; }
fi
mkdir -p "$output_dir"
stage=$(mktemp -d "$output_dir/macos-stage.XXXXXX")
cmake --install "$build_dir" --prefix "$stage"
bundle="$stage/SerikaPhotoEdit.app"
[ -d "$bundle" ] || { echo "Missing installed app bundle: $bundle" >&2; exit 1; }
mkdir -p "$bundle/Contents/Resources/documentation"
cp -R "$stage/share/serika-photoedit/." "$bundle/Contents/Resources/documentation/"
if [ -n "${ICON_PNG:-}" ]; then
    iconset="$stage/SerikaPhotoEdit.iconset"
    mkdir -p "$iconset"
    for size in 16 32 128 256 512; do
        sips -z "$size" "$size" "$ICON_PNG" --out "$iconset/icon_${size}x${size}.png" >/dev/null
        doubled=$((size * 2))
        sips -z "$doubled" "$doubled" "$ICON_PNG" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
    done
    iconutil -c icns "$iconset" -o "$bundle/Contents/Resources/SerikaPhotoEdit.icns"
    /usr/libexec/PlistBuddy -c 'Add :CFBundleIconFile string SerikaPhotoEdit.icns' "$bundle/Contents/Info.plist"
fi
set -- "$bundle" -always-overwrite
if [ -n "${CODESIGN_IDENTITY:-}" ]; then
    set -- "$@" "-sign-for-notarization=$CODESIGN_IDENTITY"
fi
"$deploy_tool" "$@"
if [ -n "${CODESIGN_IDENTITY:-}" ]; then
    codesign --verify --deep --strict "$bundle"
fi
version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$bundle/Contents/Info.plist")
architectures=$(lipo -archs "$bundle/Contents/MacOS/SerikaPhotoEdit" | tr ' ' '-')
payload="$stage/dmg"
mkdir -p "$payload"
mv "$bundle" "$payload/"
ln -s /Applications "$payload/Applications"
dmg="$output_dir/SerikaPhotoEdit-$version-$architectures.dmg"
hdiutil create -volname 'Serika PhotoEdit' -srcfolder "$payload" -ov -format UDZO "$dmg"
if [ -n "${CODESIGN_IDENTITY:-}" ]; then
    codesign --force --sign "$CODESIGN_IDENTITY" --timestamp "$dmg"
fi
if [ -n "${NOTARY_PROFILE:-}" ]; then
    xcrun notarytool submit "$dmg" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$dmg"
fi
hdiutil verify "$dmg"
echo "Created $dmg"
echo "Staging files retained at $stage"
