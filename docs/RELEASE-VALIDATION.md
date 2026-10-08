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

The [Unix packaging workflow](../.github/workflows/release-unix.yml) compiles the immutable v0.0.1 application source at commit `0be61509836ceba24d76f551a31cf5ff98ed76eb` with Qt 6.8.3, using current packaging scripts and refreshed documentation/notices. This extends the existing release without changing its tag or application version. The annotated tag object is `73888a8e939b1669dfb7c9928c4910e7e1f7b1c2`; the source commit above is its peeled application revision.

Bundled documentation is a packaging-time snapshot. See the [live validation record](https://github.com/serika-dev/SerikaPhotoEdit/blob/main/docs/RELEASE-VALIDATION.md) and [release page](https://github.com/serika-dev/SerikaPhotoEdit/releases/tag/v0.0.1) for results and download availability recorded after that snapshot.

### Linux x86_64 and aarch64

The successful Linux jobs are [x86_64](https://github.com/serika-dev/SerikaPhotoEdit/actions/runs/37785285705/job/113338352843) and [aarch64](https://github.com/serika-dev/SerikaPhotoEdit/actions/runs/37785285705/job/113338352298). Their packaging revision is `6dd48f7b7b08e19f59c0ba9df7b26f3503e5a40c`. These are the successful Linux jobs within a run whose macOS jobs did not complete package validation; the complete run is not an all-platform success.

- Both native Ubuntu 24.04 architectures compiled with warnings treated as errors and passed all eight CTest suites: 227 QtTest checks each, with no failures or skips. Qt PDF, Image Formats and LibRaw features were retained. The application dependency versions are Qt 6.8.3, LibRaw 0.21.2 and zlib 1.3; the separately embedded AppImage runtime uses zlib 1.3.2.
- Both jobs created tar.gz, AppImage and DEB downloads. Clean-environment checks exercised the staged application version, demo screenshot, batch resize/save, SPE reopen/resave and a document screenshot. XCB launch and capture passed under Xvfb.
- Extracted tar.gz payloads passed executable/version and screenshot checks. Extracted DEB payloads launched through their installed launcher layout. AppImages passed version and screenshot checks using extraction mode, without requiring FUSE. Normal DEB installation/uninstallation and FUSE-mounted AppImage execution were not exercised.
- The downloaded final archives have the expected ELF architectures, tagged application revision and packaging provenance. Dependency inventories retain exact Ubuntu binary/source package versions, owning-package copyright files and referenced common license texts. Required notice paths were present.
- Both AppImage runtime headers and pinned SHA-256 values were checked. The embedded-library notices include the matching zlib 1.3.2 text; the [supplemental complete runtime notices](https://github.com/serika-dev/SerikaPhotoEdit/releases/download/v0.0.1/AppImage-runtime-DEPENDENCY-NOTICES.txt) also retain the zstd-bundled xxHash header credit.

The Linux baseline is Ubuntu 24.04/glibc 2.39 on the matching architecture. Qt/application libraries are bundled; glibc, graphics drivers, a desktop session and fonts remain host requirements. Other distributions and older glibc have not been validated.

### macOS Intel and Apple Silicon

The Apple Silicon job in [its native run](https://github.com/serika-dev/SerikaPhotoEdit/actions/runs/37786036463) passed, using packaging revision `c4217d8d2c3f13ff525c34766f0bd144bfbce507`. Its containing run is not an all-platform success. Final Intel package validation is awaiting [the dedicated x86_64 retry](https://github.com/serika-dev/SerikaPhotoEdit/actions/runs/37787300080), using packaging revision `09837e33f211718e346be1bfe32756079608a5b1`.

- The native macOS 14 arm64 build passed all eight CTest suites: 227 QtTest checks, with no failures or skips. Its build record identifies Qt 6.8.3, LibRaw 0.22.2 and SDK/system zlib 1.2.12.
- Both its extracted ZIP and mounted DMG passed version checks, batch resize/invert/save, SPE reopen/resave, native Cocoa demo screenshots and document-reopen screenshots. The build Qt kit and Homebrew Cellar were temporarily hidden while those packaged applications ran with a clean environment and isolated preferences.
- The arm64 bundle passed strict signature verification and Mach-O architecture/dependency checks. Its dependency manifest records minimum macOS 14.0, tagged application source and the Homebrew source/version inventory. The DMG editor screenshot was visually inspected.

The bundles use ad-hoc signing without Developer ID signing or Apple notarization. Exact Homebrew dependency versions/source metadata and license files are retained in each app bundle. Native Intel/macOS 15 package evidence will be recorded after its final job completes.

Full interactive desktop acceptance outside Windows 11, Developer ID signing/notarization and universal Mac builds, normal DEB installation/uninstallation, other OS versions, tablets, mixed-DPI displays and externally produced large PSD/RAW corpora remain unverified. See [IMPLEMENTATION-STATUS.md](IMPLEMENTATION-STATUS.md) for the full feature limits.
