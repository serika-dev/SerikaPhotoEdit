# Release validation — 8 October 2026

Host: Windows 11 x64, AMD Ryzen 5 5500U. Release compiler: MinGW GCC 13.1. Qt 6.8.3, LibRaw 0.22.2 and zlib 1.3.1.

- Native application and its two internal libraries compiled with `SERIKA_WARNINGS_AS_ERRORS=ON`.
- Five CTest suites passed. QtTest totals: core 79, I/O 28, UI 8, canvas 16, actions 9; 140 passing rows including lifecycle slots.
- The UI suite exercised a real New Document dialog, brush mouse gestures, keyboard undo/redo, layer visibility/order, Home visibility and four distinct vertically stacked Essentials docks.
- Native Windows Home/editor captures were visually inspected. The panel stacking and layer-panel sizing found during visual checks were corrected.
- Portable GUI launched with PATH reduced to the Windows system directories and exited successfully after capturing its window.
- The final portable `--batch` entry point resized a PNG to 64×64, applied Invert and saved SPE, with clean PATH and exit 0. The saved document reopened successfully in the native UI with exit 0.
- ZIP required-file inventory passed: executable, Qt plugins, LibRaw, zlib, offscreen platform plugin, image codecs, documentation and example action. ZIP CRC validation returned no corrupt entries.
- WiX produced an x64 MSI. Its database contains the default application feature and a separate, optional Level 2 SPE/PSD/PSB file-association feature. Registry open command includes quoted executable and file paths.
- `msiexec /a ... /qn TARGETDIR=...` administratively extracted the MSI successfully into a workspace test directory. Its extracted application launched with clean PATH and exit 0. This is a payload validation; installation, association changes and uninstall were not performed on the user's Windows installation.
- Both the executable and installer are unsigned. WiX reports CPack-template upgrade/shortcut ICE61/69/90 warnings; MSI generation and administrative extraction succeeded. Interactive installer behavior still requires a normal installation/uninstallation acceptance run.
- Exact artifact hashes are in `dist/SHA256SUMS.txt`. Build logs and smoke outputs are under ignored `build/`.

The benchmark results are in [performance.json](performance.json): 4000×3000, ten 8-bit raster layers; 539.16 ms cold composition, 5.02 ms median cached viewport zoom, 3.24 ms brush dab plus dirty-tile rebuild, 557.02 ms redraw after undo. These are CPU measurements of one scenario, not a guarantee for all documents or hardware.

macOS/Linux recipes, ARM64, other Windows versions, tablet hardware, mixed-DPI displays and externally produced large PSD/RAW corpora remain unverified. See [IMPLEMENTATION-STATUS.md](IMPLEMENTATION-STATUS.md) for the full feature limits.
