# Contributing to Serika PhotoEdit

Serika is an early-stage C++20/Qt Widgets editor. Focused fixes, reproducible bug reports, tests and documentation improvements are welcome. For larger features, open an issue describing the intended behavior and constraints before starting a broad change.

## Report a bug

Use [GitHub Issues](https://github.com/serika-dev/SerikaPhotoEdit/issues). Include the application version, operating system/architecture, exact reproduction steps, expected and observed behavior, and relevant screenshots or logs. A small non-sensitive sample image or SPE file helps with editing and import/export bugs. For a source build, include the compiler, Qt version and CMake options. Do not upload private photographs or documents without permission.

Check [implementation status](docs/IMPLEMENTATION-STATUS.md) and [format fidelity notes](src/io/README.md) when reporting a known limitation. Confirmed limitations can still be useful issues if you describe a concrete workflow that needs them.

## Dependencies

- CMake 3.25+, Ninja or another CMake generator, and a C++20 compiler.
- Qt 6.8+ desktop kit: Core, Gui, Widgets, PrintSupport, Svg and Test.
- Optional Qt Image Formats plugins for additional codecs and Qt Pdf for PDF import.
- Optional dynamically linked LibRaw and zlib development packages for RAW and compression support.

The locally verified Windows kit is Qt 6.8.3 with MinGW GCC 13.1 x64, LibRaw 0.22.2 and zlib 1.3.1. Qt, compiler and library ABIs must match. Install tools independently; the ignored `.tools` directory is not part of the source download. Disabling optional dependencies changes the available features and which tests can run.

```sh
git clone https://github.com/serika-dev/SerikaPhotoEdit.git
cd SerikaPhotoEdit
```

## Windows build

For the tested MinGW route, install a matching Qt desktop kit and compiler, plus CMake and Ninja on PATH. Adjust the example SDK paths to your installation:

```powershell
$qtRoot = 'C:/Qt/6.8.3/mingw_64'
$compilerBin = 'C:/Qt/Tools/mingw1310_64/bin'
$env:PATH = "$qtRoot/bin;$compilerBin;$env:PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_PREFIX_PATH="$qtRoot" -DSERIKA_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/SerikaPhotoEdit.exe --demo
```

For MSVC, use the matching Qt MSVC kit from a Visual Studio developer shell and omit `-DCMAKE_CXX_COMPILER=g++`. That compiler route has not been validated locally for this release. Use a fresh build directory when changing kits or compilers.

Add an optional dependency prefix to `CMAKE_PREFIX_PATH` with a semicolon-separated list, or provide `LIBRAW_INCLUDE_DIR`, `LIBRAW_LIBRARY` and CMake's zlib variables explicitly. For a build without RAW discovery, add `-DSERIKA_WITH_RAW=OFF`.

The Windows convenience script also accepts an external SDK:

```powershell
./scripts/build.ps1 -QtRoot $qtRoot -Test -ConfigureArgs '-DSERIKA_WARNINGS_AS_ERRORS=ON'
```

## macOS and Linux build

Native macOS Intel/Apple Silicon and Linux x86_64/aarch64 release builds each passed all 227 checks without failures or skips, followed by package smoke checks. Exact successful jobs, dependency versions and package checks are linked in the [live validation record](https://github.com/serika-dev/SerikaPhotoEdit/blob/main/docs/RELEASE-VALIDATION.md). Install a compatible compiler, CMake/Ninja, Qt 6.8+ and optional development libraries, then select your Qt kit:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="/path/to/Qt/6.8.3/kit" -DSERIKA_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Alternatively, `./scripts/build-unix.sh -DCMAKE_PREFIX_PATH=/path/to/Qt/kit` configures, builds and tests. Debian-family package names include `libraw-dev`, `zlib1g-dev`, `cmake`, `ninja-build` and `pkg-config`; Qt package names commonly include `qt6-base-dev`, `libqt6svg6-dev` and `qt6-image-formats-plugins`. A newer Qt SDK is required when distribution packages are below 6.8. On macOS, LibRaw and pkg-config can be installed through Homebrew. Universal macOS builds require universal Qt and third-party dependencies; they are not validated.

For Qt 6.8.3, [aqt](https://aqtinstall.readthedocs.io/en/stable/cli.html) provides `linux_gcc_64` under host `linux`, `linux_gcc_arm64` under host `linux_arm64`, and the universal `clang_64` kit under host `mac`. Install `qtimageformats` and `qtpdf` to retain those release features. Native GitHub runner labels are `ubuntu-24.04`, `ubuntu-24.04-arm`, `macos-15-intel` and `macos-14` (Apple Silicon); see [GitHub's runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).

Select one native Mac target with `-DCMAKE_OSX_ARCHITECTURES=x86_64` or `arm64`. Its deployment target must be at least the minimum required by every bundled dependency. The Unix release workflow requests macOS 15 Intel/macOS 14 ARM and Ubuntu 24.04/glibc 2.39 Linux; older systems and arbitrary distributions are not established by those jobs.

## Test a change

CTest runs eight QtTest suites on Qt's offscreen platform: `core`, `io`, `ui`, `canvas`, `actions`, `shortcuts`, `mask` and `workflow`. The verified Windows x64, Linux x86_64/aarch64 and macOS Intel/Apple Silicon 0.0.1 baseline is **227 passing checks** per target, including lifecycle slots and data rows, with all optional release dependencies present. Other dependency configurations may skip unsupported-format checks.

```sh
ctest --test-dir build --output-on-failure
ctest --test-dir build -R 'canvas|shortcuts' --output-on-failure
```

Each suite writes a text result file in the build directory. Keep tests meaningful: verify pixels, native precision, document state and commit/cancel/undo behavior where relevant. Run the affected suites, then the full suite before submitting. Manually check visible UI changes on a native display; offscreen tests do not establish visual quality, tablet behavior or mixed-DPI support.

The standalone benchmark uses a 4000 × 3000 document with ten raster layers:

```sh
./build/performance_bench -platform offscreen --output build/performance.json
```

On Windows, the executable is `performance_bench.exe`. Include the hardware, build type and scenario when discussing measurements; one benchmark does not guarantee performance for complex documents.

## Submit a pull request

- Keep the change focused and explain the concrete problem and resulting behavior.
- Follow surrounding C++/Qt conventions and the repository's `.clang-format` settings. Avoid unrelated formatting changes.
- Preserve 8/16/float pixel and mask precision. Treat a complete gesture or accepted dialog as one undo step, and ensure Escape/cancellation preserves document state.
- Add a regression test when behavior warrants one. List the tests and manual checks you ran, plus any unverified platforms or material limits.
- Update user-facing documentation when commands, shortcuts, formats or supported behavior change.
- Keep generated builds, packages, credentials and private test images out of commits. Credit third-party dependencies and preserve their license notices.

`src/document` contains state/tiles/history, `src/compositor` rendering, `src/ui/canvas` and `src/tools` gestures, `src/io` formats, `src/filters` processing, `src/actions` reproducible operations, and `src/ui` chrome/dialogs. Coordinate changes to shared document and canvas interfaces across callers.

Contributions are accepted under the repository's [MIT license](LICENSE).

## Package a local build

Windows staging uses `windeployqt` from the same kit used to compile. ZIP packaging needs CPack; MSI packaging additionally needs WiX 3 `candle.exe` and `light.exe` on PATH:

```powershell
./scripts/package-windows.ps1 -QtRoot $qtRoot -SkipMsi
# With WiX 3 installed, omit -SkipMsi to also create the MSI.
```

`scripts/package-linux.sh` stages a native Ubuntu 24.04 build, retains Qt/application dependencies and Ubuntu license/source inventories, and creates tar.gz and DEB packages. Set both `APPIMAGETOOL` and `APPIMAGE_RUNTIME` to verified native binaries to add an AppImage. The release workflow pins their SHA-256 hashes and the runtime source provenance. The script exercises extracted packages with clean environment settings, batch save/reopen, screenshots and an XCB display under Xvfb. DEB payload extraction is distinct from installing/uninstalling a package on a user's desktop.

`scripts/package-macos.sh` stages the app, resolves and audits bundled Qt/Homebrew dependencies, selects `RELEASE_ARCH=x86_64` or `arm64`, records the dependency-derived minimum OS version and creates DMG/ZIP packages. Its default signature is ad-hoc; Developer ID signing/notarization requires the packager's own credentials. `scripts/smoke-macos.sh` exercises extracted ZIP and mounted DMG payloads. Full interactive native acceptance still requires a desktop review.

The [Unix release workflow](.github/workflows/release-unix.yml) checks out the existing application release tag separately from the current packaging scripts. It overlays documentation and license files, and verifies that application source and CMake files remain unchanged. Flatpak, Nix, RPM and universal Mac recipes remain separate, unverified routes. Packaging recipes alone do not establish support; record the actual build, dependency and launch results before publishing a package.
