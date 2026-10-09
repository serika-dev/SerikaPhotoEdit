# Changelog

## Unreleased

- Optional OpenGL 3.3 GPU adjustments and Gaussian blur, CPU fallback, persistent preference and device diagnostics.
- Real LittleCMS ICC CMYK soft proofing, intents/BPC, paper simulation, gamut warnings and 8/16-bit CMYK TIFF/ink separations. Layer editing remains RGB.
- Editable mixed typography, paragraph metrics, text on paths and shared render/outline layout.
- Native stdio MCP server with workspace access controls, isolated documents, previews, layers, native saves, atomic actions and undo/redo.

- Serika brand refresh: violet accents, plum and lavender surfaces, bundled Outfit headings and Onest interface text across all four themes.
- New home screen featuring the supplied PhotoEdit mascot, with matching Windows, Linux and macOS application icons.
- Theme-aware tool icons, selection controls and preview surfaces, with visible keyboard focus and neutral image surrounds.
- Multi-layer alignment, distribution, grouping, duplication, deletion and stack ordering, with stable selections and undo.
- Editable embedded Smart Object documents, linked placement/reload/relink, replacement and export, with retained transforms, masks and Smart Filters.
- Advanced brush dynamics, deterministic stroke spacing, pressure/tilt controls, stroke previews and saved/imported/exported Serika brush presets.
- Draggable RGB and per-channel Curves, histogram overlays, numeric point editing and curve presets.
- Real Pattern Fill layers and five gradient styles with multiple color/opacity stops, plus an editable Gradient Map ramp.
- Multi-layer Move and keyboard nudging preserve selections and unlinked masks, with gesture cancellation and atomic undo.
- Windows x64 Release: all 17 suites pass (383 checks; 38 hardware-only cases skipped headlessly). Native AMD GPU run: 44 passed, zero failures, one expected unavailable-device skip. Fourteen UI captures reviewed. These features remain unreleased; current Windows/Linux/macOS CI now requires LittleCMS.

## 0.0.1 — 2026-10-08

Initial public release under the MIT license, with source and Windows x64 portable ZIP/MSI downloads. The same release also provides verified Linux x86_64/aarch64 tar.gz, AppImage and DEB packages and native macOS Intel/Apple Silicon DMG/ZIP packages, using unchanged v0.0.1 application source.

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
- Native Ubuntu 24.04 x86_64/aarch64, macOS 15 Intel and macOS 14 Apple Silicon builds each passed all 227 checks without failures or skips. Linux package smoke checks and both Mac architectures' ZIP/DMG batch, SPE reopen and Cocoa screenshot checks passed.
- Windows executable and MSI are unsigned. Mac bundles use ad-hoc signatures without Developer ID signing/notarization; their minimums are macOS 15.0 Intel and 14.0 Apple Silicon. Exact successful jobs are linked in the [live validation record](https://github.com/serika-dev/SerikaPhotoEdit/blob/main/docs/RELEASE-VALIDATION.md). Windows ARM64 remains unverified.
- This early release does not provide full Photoshop parity. Advanced interchange, typography, color-management, semantic content-aware tools and GPU acceleration remain limited or absent.

See the [0.0.1 release notes](docs/releases/0.0.1.md), [validation record](docs/RELEASE-VALIDATION.md) and [implementation status](docs/IMPLEMENTATION-STATUS.md) for details.
