# Changelog

## 0.0.1 — 2026-10-08

Initial public release under the MIT license, with source and Windows x64 portable ZIP/MSI downloads.

### Added

- Native Qt Widgets editor with tabs, detachable windows, dockable panels, workspaces, command palette and four themes.
- High-precision RGB layers, selections and linked raster/vector masks; groups, adjustments, layer effects, smart objects and copy-on-write undo.
- Brush/eraser, clone/heal, fill/gradient, selection, shape, text and path editing, guides/rulers and Liquify.
- Crop/perspective previews with commit/cancel, ratios, output size, overlays, straighten and automatic bounds detection; inline transforms, Patch and Content-Aware Move.
- Select and Mask refinement, saved selections, layer comps, selected History Brush sources, Fade and editable retained-source smart-filter stacks.
- Remappable Photoshop-style shortcuts, conflicts, held modifiers, tool cycles and JSON import/export.
- Native SPE/SPEB, PSD/PSB interchange, LibRaw RAW Develop and raster/HDR/SVG/PDF import workflows.
- Actions, parameter playback and headless folder batch processing.

### Validation and known limits

- Windows 11 x64 Release build passed all eight CTest suites: **227 QtTest checks**, including lifecycle slots and data-driven rows.
- Portable GUI/batch smoke checks, ZIP integrity and MSI administrative extraction passed. Interactive installation, file associations and uninstall remain unverified.
- Executable and MSI are unsigned. macOS/Linux/ARM64 recipes have no verified release binaries.
- This early release does not provide full Photoshop parity. Advanced interchange, typography, color-management, semantic content-aware tools and GPU acceleration remain limited or absent.

See the [0.0.1 release notes](docs/releases/0.0.1.md), [validation record](docs/RELEASE-VALIDATION.md) and [implementation status](docs/IMPLEMENTATION-STATUS.md) for details.
