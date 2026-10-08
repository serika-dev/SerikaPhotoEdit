# Serika PhotoEdit

A native C++20 / Qt 6.8 Widgets image editor, version **1.0.0-ultra**. Original Serika icons, fox-amber accents, four interface themes, a tiled layer engine and real editing tools.

This repository implements a substantial working editor from the supplied brief. It is **not a complete implementation of every acceptance item**. Read [the implementation status](docs/IMPLEMENTATION-STATUS.md) and [format fidelity notes](src/io/README.md) before relying on advanced features or interchange with another editor.

![Serika PhotoEdit editor](docs/screenshots/editor.png)

## Run on this Windows host

Double-click `dist/portable/bin/SerikaPhotoEdit.exe`, or install the MSI in `dist`. The portable directory and ZIP include the Qt, MinGW, LibRaw and zlib runtime libraries; a Qt installation is not required to run them.

```powershell
./dist/portable/bin/SerikaPhotoEdit.exe
./dist/portable/bin/SerikaPhotoEdit.exe --demo
```

Home offers an original layered **Amber Valley** project. Open an image, add a layer, choose Brush (`B`), paint, and undo (`Ctrl+Z`). Save layered work as `.spe`; export a flattened image through File → Export. Editable Serika fields exported to PSD use private tags and raster compatibility layers, so use SPE as the master document.

## Implemented workflows

- Native document tabs, detachable windows, dockable panels, resettable workspaces, command palette, recent files, themes and a configurable Photoshop-style shortcut registry with conflict detection, tool cycles, held modifiers and JSON import/export.
- Sparse 256 × 256 tiles, 8-bit / 16-bit / float RGB, copy-on-write history, selections, masks, pixel / text / shape / group / adjustment / smart-object layer data.
- CPU compositor with 27 blend modes, clipping, group isolation and pass-through, masks, adjustment layers and layer-effect rendering.
- Brush, eraser, clone/heal, selection, crop and perspective crop, move, gradient/fill, pen/anchor editing, vector shapes, text, zoom/pan, rulers/guides and interactive Liquify.
- Native SPE/SPEB with checksums and atomic saving; original PSD/PSB reader/writer; LibRaw RAW import and Develop; common raster formats, TGA, float HDR, SVG rasterization and PDF page import.
- CPU filters and adjustments, editable retained-source smart-filter stacks, action recording with parameter capture, validated action playback and folder batch processing.
- Raster/vector masks with density, feather and linking; native 8/16/float selection coverage; Select and Mask preview/edge brush/output modes; persistent selection channels and layer comps.
- Preview-first crop with ratios, output size, overlays, straighten and automatic bounds detection; inline Free Transform handles; real Patch and Content-Aware Move transport.

Several specialist operations use simplified CPU algorithms. The status document lists material limits, including Adobe descriptor fidelity, embedded subdocuments, color modes, advanced typography, GPU acceleration and packaging verification. The shortcut reference is in [docs/KEYBOARD-SHORTCUTS.md](docs/KEYBOARD-SHORTCUTS.md).

## Build

Requires CMake ≥ 3.25, Ninja or another CMake generator, a C++20 compiler and **Qt ≥ 6.8** with Core, Gui, Widgets, PrintSupport, Svg and Test. Qt Image Formats adds TIFF/WebP/etc.; Qt Pdf enables PDF import. Dynamically linked LibRaw and zlib are detected when available. Development packages older than Qt 6.8 need a newer Qt SDK.

The tools prepared on this host are under ignored `.tools`. Rebuild and test here:

```powershell
./scripts/build.ps1 -Test
./scripts/build.ps1 -Test -Package
```

For a separately installed Qt SDK:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/compiler \
  -DSERIKA_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Windows SDK and compiler architectures must match. The verified host uses Qt 6.8.3, MinGW 13.1, LibRaw 0.22.2 and zlib 1.3.1, all x64. To use another compiler or architecture, configure a separate build directory with matching dependencies. `SERIKA_WITH_RAW=OFF` disables LibRaw discovery. Optional codecs are advertised only when a matching Qt image plugin is installed.

## Platform and packaging coverage

Only **Windows 11 x64** has been compiled, tested and packaged in this workspace. Other rows are source/build targets and recipes, not verified release binaries. The GitHub Actions workflow defines Windows, macOS and Ubuntu builds; it has not been run remotely in this session.

| Platform requested in the brief | Route | Verification |
| --- | --- | --- |
| Windows 10/11 x64 | Qt 6.8 SDK; Ninja; `scripts/package-windows.ps1`; portable ZIP and WiX MSI | Windows 11 x64 verified |
| Windows ARM64 | Matching native ARM64 Qt and compiler; same CMake project | Unverified; no ARM64 binary supplied |
| macOS 12+ Intel / Apple Silicon | Qt 6.8 SDK; CMake app bundle; CPack DragNDrop DMG | Recipe only; universal build requires universal dependencies |
| Ubuntu 22.04/24.04/26.04; Debian 12/13 | Qt 6.8 SDK or suitably new distro packages; CPack DEB | Recipe only |
| Mint 21/22; Pop!_OS 22.04; elementary 7/8; Zorin 17 | Ubuntu route with a Qt 6.8 SDK when distro Qt is older | Recipe only |
| Fedora 40/41/42 | Qt 6.8 development packages/SDK; CPack RPM | Recipe only |
| Arch / Manjaro; openSUSE Leap 15.6 / Tumbleweed | Qt 6.8+; CMake/Ninja; TGZ, RPM where appropriate | Recipe only |
| NixOS / Nix | `packaging/nix/flake.nix` | Recipe only |
| Alpine (optional) | Native musl Qt ≥6.8; `SERIKA_ALPINE=ON`; system libraries | Experimental, unverified |
| FreeBSD 14 (optional) | Qt ≥6.8 ports/packages and native CMake build | Experimental, unverified |

Linux common dependency names are `qt6-base-dev`, `libqt6svg6-dev`, `qt6-image-formats-plugins`, `libraw-dev`, `zlib1g-dev`, `cmake`, `ninja-build` (Debian family), or their distro equivalents. Check that the selected Qt meets the required version.

```sh
./scripts/build-unix.sh -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --install build --prefix "$PWD/dist/stage"
cpack --config build/CPackConfig.cmake -G TGZ
# On Debian-family builders with dpkg-shlibdeps:
cpack --config build/CPackConfig.cmake -G DEB
# On RPM builders:
cpack --config build/CPackConfig.cmake -G RPM
# AppImage uses an externally supplied linuxdeploy + Qt plugin:
./scripts/appimage.sh
# Flatpak recipe:
flatpak-builder --user --install build-flatpak packaging/flatpak/io.serika.PhotoEdit.yml
# Nix recipe:
nix build ./packaging/nix
```

macOS packaging uses `scripts/package-macos.sh` or `cpack -G DragNDrop`; signing and notarization require the developer's own Apple credentials. The script accepts `CODESIGN_IDENTITY` and `NOTARY_PROFILE`. Windows MSI uses WiX 3.x and provides an optional file-association feature. Builds are unsigned.

## Verified results on this host

On 8 October 2026, the Release build completed with compiler warnings treated as errors. All eight CTest suites passed; QtTest reported **227 passing checks** (79 core, 37 I/O, 8 UI, 34 canvas, 13 actions, 14 shortcuts, 21 masks and 21 workflows, including lifecycle slots and data rows). Native Windows screenshots were captured and inspected. Portable launch and a headless resize/invert/SPE batch were exercised with the development-tool directories removed from PATH.

The Windows portable ZIP and WiX MSI were generated. MSI install/uninstall and file-association changes have not been exercised against the user's Windows installation. The installer and executable are unsigned.

The ten-layer benchmark on the host's AMD Ryzen 5 5500U measured 539 ms cold composition, 5.02 ms median cached viewport zoom, 3.24 ms brush dab plus dirty-region composition, and 557 ms redraw after undo. These measurements describe this specific CPU raster scenario; complex group/effect stacks may require a full rebuild.

## Tests and performance

CTest runs eight QtTest suites: document/compositor, formats, native UI, canvas/tools, actions, shortcuts, masks and integrated workflows. Tests exercise high-precision samples, blend formulas, history isolation, corrupt files, PSD/PSB channels and masks, synthetic DNG decoding, PDF and raster imports, canvas gestures, crop commit/cancel, linked/unlinked mask transforms, retained smart sources, actual dialog cancellation/acceptance, shortcut remapping, layer order, clipboard coordinates and headless actions. Passing tests establish those behaviors; they do not certify all items in the supplied brief.

```powershell
./build/performance_bench.exe -platform offscreen --output docs/performance.json
```

The benchmark measures a 4000 × 3000 document with ten raster layers, cold and cached compositing, a resampled viewport, a dirty-tile brush update and undo. Results in `docs/performance.json` are host measurements, not cross-platform or guaranteed frame-rate claims.

## Headless actions

Actions are JSON objects with a `steps` array. Each step contains a `command`, optional `name` and `parameters`. The included example resizes images and applies an exposure adjustment:

```powershell
./dist/portable/bin/SerikaPhotoEdit.exe --batch examples/web-export.speaction input-folder output-folder
```

Batch writes SPE files. Supported operations fail explicitly on invalid parameters or unknown commands, and playback rolls back the document on failure. GUI recording captures supported commands and accepted adjustment/filter settings; it does not record arbitrary pointer gestures.

## Repository

`src/document` owns state, tiles and history; `src/compositor` owns rendering; `src/tools` and `src/ui/canvas` own gestures; `src/io` owns files; `src/filters` owns CPU processing; `src/actions` owns reproducible operations; `src/ui` owns native chrome and dialogs. Theme tokens and original SVG icons are in `resources`. `docs/ENGINE-CONTRACT.md` records module boundaries.

Application code and original icons are MIT licensed. Shared libraries and codecs retain their own licenses; see [third-party notices](docs/THIRD-PARTY.md) and `resources/licenses`.
