#!/usr/bin/env sh
# Smoke-test an extracted ZIP or mounted DMG on its native architecture.
# The application is run with a clean environment and isolated preferences.
# Qt/Homebrew runtime prefixes are temporarily renamed when writable, then restored.
set -eu
[ "$(uname -s)" = Darwin ] || { echo 'Run this script on macOS.' >&2; exit 1; }
[ "$#" = 2 ] || { echo 'Usage: smoke-macos.sh PACKAGE.zip-or-dmg OUTPUT_DIRECTORY' >&2; exit 2; }
package_dir=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd)
package="$package_dir/$(basename -- "$1")"
mkdir -p "$2"
output=$(CDPATH= cd -- "$2" && pwd)
scratch=$(mktemp -d "${TMPDIR:-/tmp}/serika-smoke.XXXXXX")
mkdir -p "$scratch/home" "$scratch/tmp" "$scratch/input" "$scratch/output" "$scratch/reopened"
hidden_file="$scratch/hidden-prefixes.txt"
: > "$hidden_file"
mountpoint=
child=
cleanup() {
    set +e
    if [ -n "$child" ]; then kill "$child" 2>/dev/null; fi
    # Restore ancestors before children if a Qt kit was inside Homebrew's Cellar.
    /usr/bin/tail -r "$hidden_file" | while IFS='|' read -r original hidden; do
        [ ! -d "$hidden" ] || /bin/mv "$hidden" "$original"
    done
    [ -z "$mountpoint" ] || /usr/bin/hdiutil detach "$mountpoint" >/dev/null
}
trap cleanup EXIT HUP INT TERM
case "$package" in
    *.zip)
        /usr/bin/ditto -x -k "$package" "$scratch/extracted"
        bundle="$scratch/extracted/SerikaPhotoEdit.app"
        ;;
    *.dmg)
        mountpoint="$scratch/mounted"
        mkdir -p "$mountpoint"
        /usr/bin/hdiutil attach -readonly -nobrowse -mountpoint "$mountpoint" "$package" > "$output/mount.log"
        bundle="$mountpoint/SerikaPhotoEdit.app"
        ;;
    *) echo 'Expected a .zip or .dmg package.' >&2; exit 2 ;;
esac
executable="$bundle/Contents/MacOS/SerikaPhotoEdit"
[ -x "$executable" ] || { echo 'Package lost its executable or executable permission.' >&2; exit 1; }
/usr/bin/codesign --verify --deep --strict "$bundle" > "$output/codesign.log" 2>&1
architecture=$(/usr/bin/lipo -archs "$executable")
[ "$architecture" = "$(uname -m)" ] || { echo "Package architecture is $architecture, runner is $(uname -m)." >&2; exit 1; }
[ -f "$bundle/Contents/Resources/dependency-manifest.json" ] || { echo 'Dependency manifest missing.' >&2; exit 1; }
/usr/bin/ditto "$bundle/Contents/Resources/dependency-manifest.json" "$output/dependency-manifest.json"
python3 - "$scratch" <<'PY'
import json, pathlib, struct, sys, zlib
root = pathlib.Path(sys.argv[1])
def chunk(name, payload):
    return struct.pack(">I", len(payload)) + name + payload + struct.pack(">I", zlib.crc32(name + payload) & 0xffffffff)
pixels = b"".join(b"\0" + b"".join(bytes((240, 80, 24, 255)) if (x // 8 + y // 8) % 2 else bytes((24, 80, 240, 255)) for x in range(48)) for y in range(32))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 48, 32, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b"")
(root / "input/fixture.png").write_bytes(png)
(root / "action.json").write_text(json.dumps({"version": 1, "steps": [{"command": "resize", "parameters": {"width": 64, "height": 64}}, {"command": "adjustment", "name": "Invert", "parameters": {"destructive": True}}]}))
(root / "reopen.json").write_text(json.dumps({"version": 1, "steps": []}))
PY
hide_prefix() {
    original=$1
    [ -n "$original" ] && [ -d "$original" ] || return 0
    case "$original" in /|/usr|/usr/local|/opt|/opt/homebrew) echo "Refusing broad runtime-prefix isolation: $original" >&2; exit 1 ;; esac
    hidden="$original.serika-smoke-hidden.$$"
    [ ! -e "$hidden" ] || { echo "Isolation target already exists: $hidden" >&2; exit 1; }
    if /bin/mv "$original" "$hidden"; then
        printf '%s|%s\n' "$original" "$hidden" >> "$hidden_file"
        printf 'Temporarily hid %s\n' "$original" >> "$output/isolation.log"
    else
        echo "Could not isolate runtime prefix: $original" >&2
        exit 1
    fi
}
# Resolve prefixes before hiding them. No Homebrew/Python tools run while hidden.
cellar=
if command -v brew >/dev/null; then cellar=$(brew --cellar); fi
qt_prefix=${QT_ROOT_DIR:-}
[ -n "$qt_prefix" ] || { echo 'Set QT_ROOT_DIR so the build Qt kit can be isolated.' >&2; exit 1; }
[ -x "$qt_prefix/bin/macdeployqt" ] || { echo "Not a Qt kit: $qt_prefix" >&2; exit 1; }
hide_prefix "$qt_prefix"
[ -z "$cellar" ] || hide_prefix "$cellar"
run_clean() {
    log=$1
    shift
    /usr/bin/env -i PATH=/usr/bin:/bin:/usr/sbin:/sbin HOME="$scratch/home" TMPDIR="$scratch/tmp/" LC_ALL=C "$@" > "$log" 2>&1 &
    child=$!
    elapsed=0
    while /bin/kill -0 "$child" 2>/dev/null; do
        if [ "$elapsed" -ge 60 ]; then
            /bin/kill "$child" 2>/dev/null || true
            /bin/sleep 1
            /bin/kill -9 "$child" 2>/dev/null || true
            echo "Application timed out; see $log" >&2
            return 1
        fi
        /bin/sleep 1
        elapsed=$((elapsed + 1))
    done
    if wait "$child"; then child=; return 0; fi
    child=
    /bin/cat "$log" >&2
    return 1
}
run_clean "$output/version.log" "$executable" -platform offscreen --version
/usr/bin/grep '0.0.1' "$output/version.log" >/dev/null
run_clean "$output/batch.log" "$executable" --batch "$scratch/action.json" "$scratch/input" "$scratch/output"
[ -s "$scratch/output/fixture.spe" ] || { echo 'Batch produced no SPE document.' >&2; exit 1; }
run_clean "$output/reopen-batch.log" "$executable" --batch "$scratch/reopen.json" "$scratch/output" "$scratch/reopened"
[ -s "$scratch/reopened/fixture.spe" ] || { echo 'Saved SPE failed to reopen and resave.' >&2; exit 1; }
run_clean "$output/gui.log" "$executable" -platform cocoa --demo --screenshot "$output/gui.png"
[ -s "$output/gui.png" ] || { echo 'Native Cocoa GUI produced no screenshot.' >&2; exit 1; }
run_clean "$output/reopen-gui.log" "$executable" -platform cocoa "$scratch/output/fixture.spe" --screenshot "$output/reopened.png"
[ -s "$output/reopened.png" ] || { echo 'Native Cocoa document reopen produced no screenshot.' >&2; exit 1; }
/usr/bin/sips -g pixelWidth -g pixelHeight "$output/gui.png" "$output/reopened.png" > "$output/screenshots.txt"
/usr/bin/ditto "$scratch/output/fixture.spe" "$output/fixture.spe"
/usr/bin/ditto "$scratch/reopened/fixture.spe" "$output/reopened.spe"
printf 'PASS: %s native %s version, batch, SPE reopen, Cocoa GUI and screenshots; Qt/Cellar hidden.\n' "$(basename -- "$package")" "$architecture" | /usr/bin/tee "$output/summary.txt"
