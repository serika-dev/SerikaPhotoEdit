# Release validation — 0.0.1 (8 October 2026)

Host: Windows 11 x64, AMD Ryzen 5 5500U. Release compiler: MinGW GCC 13.1. Qt 6.8.3, LibRaw 0.22.2 and zlib 1.3.1.

- Native application and its two internal libraries compiled with `SERIKA_WARNINGS_AS_ERRORS=ON`.
- Eight CTest suites passed. QtTest totals: core 79, I/O 37, UI 8, canvas 34, actions 13, shortcuts 14, masks 21, workflows 21; 227 passing checks including lifecycle slots and data rows. No failures or skips.
- New integration checks cover crop previews/commit/cancel, exact 16/float coverage, linked/unlinked mask transforms, Select and Mask cancellation/output, editable smart-filter parameters, retained sources through SPE/PSD/PSB, grouped clipboard coordinates, safe regrouping, Merge Visible preservation, Fade, and shortcut remapping/conflicts.
- The UI suite exercised a real New Document dialog, brush mouse gestures, keyboard undo/redo, layer visibility/order, Home visibility and four distinct vertically stacked Essentials docks.
- Native Windows Home/editor captures were visually inspected. The panel stacking and layer-panel sizing found during visual checks were corrected.
- Portable GUI launched with PATH reduced to the Windows system directories and exited successfully after capturing its window.
- The final portable `--batch` entry point resized a PNG to 64×64, applied Invert and saved SPE, with clean PATH and exit 0. The saved document reopened successfully in the native UI with exit 0.
- ZIP required-file inventory passed: executable, Qt plugins, LibRaw, zlib, offscreen platform plugin, image codecs, documentation and example action. ZIP CRC validation returned no corrupt entries.
- WiX produced an x64 MSI. Its database contains the default application feature and a separate, optional Level 2 SPE/PSD/PSB file-association feature. Registry open command includes quoted executable and file paths.
- `msiexec /a ... /qn TARGETDIR=...` administratively extracted the MSI successfully into a workspace test directory. Its extracted application launched with clean PATH and exit 0. This is a payload validation; installation, association changes and uninstall were not performed on the user's Windows installation.
- Both the executable and installer are unsigned. WiX reports CPack-template upgrade/shortcut ICE61/69/90 warnings; MSI generation and administrative extraction succeeded. Interactive installer behavior still requires a normal installation/uninstallation acceptance run.
- Exact artifact hashes are in `dist/SHA256SUMS.txt`. Build logs and smoke outputs are under ignored `build/`.

The benchmark results are in [performance.json](performance.json): 4000×3000, ten 8-bit raster layers; 535.47 ms cold composition, 5.24 ms median cached viewport zoom, 4.02 ms brush dab plus dirty-tile rebuild, 453.71 ms redraw after undo. These are CPU measurements of one scenario, not a guarantee for all documents or hardware.

[GitHub Actions](https://github.com/serika-dev/SerikaPhotoEdit/actions/runs/37781612875) compiled and passed offscreen tests, runtime staging and artifact upload on Windows Server 2022, macOS 14 and Ubuntu 24.04. Qt deployment receives an absolute staging path. All three jobs passed.

## Additional Linux and macOS packages

Validation and publication of the additional platform downloads are **pending**. The [Unix packaging workflow](../.github/workflows/release-unix.yml) targets Ubuntu 24.04 x86_64 and aarch64, macOS 15 Intel and macOS 14 Apple Silicon. It compiles the immutable v0.0.1 application source at commit `73888a8e939b1669dfb7c9928c4910e7e1f7b1c2` with Qt 6.8.3, using current packaging scripts and refreshed documentation/notices. This extends the existing release without changing its tag or application version.

Planned Linux outputs are portable tar.gz, AppImage and DEB for each architecture. Qt and application libraries are bundled, with Ubuntu package copyright texts, common license texts and source/version inventories. The target baseline is Ubuntu 24.04/glibc 2.39; graphics drivers, a desktop session and fonts remain host requirements. AppImage runtime binaries are pinned by SHA-256, with their own license notices.

Planned macOS outputs are DMG and ZIP for each native architecture. Bundled Mach-O code is audited for architecture, external dependencies and minimum OS version. Requested baselines are macOS 15 Intel and macOS 14 Apple Silicon; the bundle minimum is raised if a dependency requires it. Bundles use ad-hoc signing, without Developer ID signing or Apple notarization. Exact Homebrew dependency versions/source metadata and license files are retained in each app bundle.

Build/test results, extracted-package launches, screenshot checks, batch save/reopen checks, dependency audits and final minimum OS versions will be recorded here after the jobs finish. No successful package or native-launch result is claimed while validation is pending.

Full interactive desktop acceptance outside Windows 11, Developer ID signing/notarization and universal Mac builds, normal DEB installation/uninstallation, other OS versions, tablets, mixed-DPI displays and externally produced large PSD/RAW corpora remain unverified. See [IMPLEMENTATION-STATUS.md](IMPLEMENTATION-STATUS.md) for the full feature limits.
