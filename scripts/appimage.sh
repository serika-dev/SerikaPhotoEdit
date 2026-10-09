#!/usr/bin/env sh
set -eu
# Run on the oldest supported glibc target using Qt >=6.8 and linuxdeploy + Qt plugin in PATH.
workspace=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_dir=${SOURCE_DIR:-"$workspace"}
case "$source_dir" in /*) ;; *) source_dir="$workspace/$source_dir" ;; esac
build_dir=${BUILD_DIR:-"$source_dir/build"}
app_dir=${APP_DIR:-"$workspace/dist/AppDir"}
output_dir=${OUTPUT_DIR:-"$workspace/dist"}
case "$build_dir" in /*) ;; *) build_dir="$workspace/$build_dir" ;; esac
case "$app_dir" in /*) ;; *) app_dir="$workspace/$app_dir" ;; esac
case "$output_dir" in /*) ;; *) output_dir="$workspace/$output_dir" ;; esac
if [ -z "${QMAKE:-}" ]; then
    if [ -n "${QT_ROOT_DIR:-}" ] && [ -x "$QT_ROOT_DIR/bin/qmake" ]; then
        QMAKE="$QT_ROOT_DIR/bin/qmake"
    elif command -v qmake6 >/dev/null 2>&1; then
        QMAKE=$(command -v qmake6)
    else
        QMAKE=$(command -v qmake) || { echo 'Qt 6 qmake was not found; set QMAKE to its absolute path.' >&2; exit 1; }
    fi
fi
export QMAKE
case "$("$QMAKE" -query QT_VERSION)" in
    6.[0-7].*) echo 'AppImage requires qmake from Qt 6.8 or newer.' >&2; exit 1 ;;
    6.*) ;;
    *) echo 'AppImage requires a Qt 6 qmake.' >&2; exit 1 ;;
esac
command -v linuxdeploy >/dev/null 2>&1 || { echo 'Install linuxdeploy and linuxdeploy-plugin-qt in PATH.' >&2; exit 1; }
icon="$source_dir/resources/icons/serika-photoedit.png"
[ -f "$icon" ] || icon="$source_dir/resources/icons/serika-photoedit.svg"
[ -f "$icon" ] || { echo "Missing application icon in SOURCE_DIR: $source_dir" >&2; exit 1; }
cmake --install "$build_dir" --prefix "$app_dir/usr"
mkdir -p "$output_dir"
cd "$output_dir"
linuxdeploy --appdir "$app_dir" --plugin qt --desktop-file "$source_dir/packaging/linux/io.serika.PhotoEdit.desktop" --icon-file "$icon" --output appimage
