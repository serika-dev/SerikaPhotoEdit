# SERIKA PHOTOEDIT — MASTER BUILD PROMPT
# Target model: GPT 6.1 Sol, Ultra mode
# Product: Serika PhotoEdit (native Qt 6 Photoshop-class editor)
# Usage: paste this entire document as the user message. Do not summarize it. Execute it.

---

You are GPT 6.1 Sol running in Ultra mode. You are the lead engineer, UI engineer, compositor author, format engineer, and release engineer for Serika PhotoEdit. Ultra mode means: plan briefly, then implement completely. Do not stop at a mock, a screenshot, a TODO list, or a “phase 1 skeleton.” Ship a compiling, launchable, native desktop application with a working document model, a real canvas, real tools, real panels, real file I/O, and installable packaging for every platform listed below. If a feature cannot be finished in the first pass, implement the data model, the UI, a correct CPU fallback, and a failing test that names the gap — never a disabled menu item with no code behind it.

Work in this order inside one repository, and keep going until the acceptance checklist at the end is green:

1. Scaffold CMake + Qt 6 Widgets app that launches on the host OS.
2. Design system and Photoshop-faithful chrome (clean-room, original icons).
3. Document model, history, tiled raster, CPU compositor.
4. Canvas interaction: zoom, pan, rulers, guides, selection.
5. Tools, options bar, contextual task bar.
6. Layers, masks, adjustment layers, blend modes, layer styles.
7. File I/O: native `.spe`, PSD/PSB, RAW, and the raster formats listed below.
8. Filters, Camera Raw develop, actions, batch.
9. Tests, benchmarks, packaging for Windows, macOS, and the Linux matrix.

Do not ask clarifying questions. The decisions in this document are final. If two interpretations exist, choose the one closer to Adobe Photoshop 2025/2026 desktop behavior, then brand it as Serika.

---

## 0. Identity

- Product name: **Serika PhotoEdit**
- Executable: `SerikaPhotoEdit` (Windows: `SerikaPhotoEdit.exe`)
- Native document: `.spe` (Serika PhotoEdit Document), plus `.speb` for documents larger than 30,000 px or 2 GB
- Version stamped in About: `1.0.0-ultra`
- Publisher string: Serika
- Tagline in About only: “Native photo editing. Layers, masks, RAW, PSD.”
- No Adobe wordmarks, no “Photoshop” in the UI chrome, no Photoshop splash, no Adobe icons, no copied toolbar PNGs, no “Ps” glyph. Matching layout, spacing, panel grammar, shortcut map, and interaction is allowed and required. Icons are original Serika geometry (2 px stroke, 18×18 and 22×22, monochrome, currentColor).
- Accent is Serika fox-amber `#E8893A`, not Adobe blue. Structure stays Photoshop-dark so a Photoshop user can sit down and work.

---

## 1. What “almost 1:1” means

Match Photoshop 2025/2026 Essentials workspace geometry and behavior. A user who knows Photoshop must find every control where muscle memory expects it.

### 1.1 Application frame (default Essentials, dark)

Measured at 100% UI scale, 1440×900 minimum, designed at 1920×1080.

```
┌──────────────────────────────────────────────────────────────────────────┐
│ Menu bar (File Edit Image Layer Type Select Filter 3D View Window Help) │ 22 px
├──────────────────────────────────────────────────────────────────────────┤
│ Options bar (tool preset, tool-specific controls, workspace switcher)   │ 31 px
├────┬─────────────────────────────────────────────────────────────┬───────┤
│Tool│ Document tab strip                                          │Panels │
│bar │ ┌─────────────────────────────────────────────────────────┐ │  right│
│ 38 │ │ canvas surround                                         │ │ dock  │
│ or │ │   ┌───────────────┐                                     │ │ 278 px│
│ 76 │ │   │ document      │  rulers optional, 20 px             │ │       │
│ px │ │   │               │                                     │ │       │
│    │ │   └───────────────┘                                     │ │       │
│    │ │ status bar: zoom, doc size, profile, scratch            │ │       │
│    │ └─────────────────────────────────────────────────────────┘ │       │
└────┴─────────────────────────────────────────────────────────────┴───────┘
```

- Menu bar: native `QMenuBar` on Windows and Linux. On macOS, the menu bar is in the system menu bar (`Qt::AA_DontUseNativeMenuBar` left false).
- Options bar sits under the menu bar, full width, 31 px, `#3A3A3A`, 1 px bottom border `#1E1E1E`.
- Toolbar docks left, single column by default (38 px including padding), toggle to two columns (76 px) via the chevron at the top of the toolbar, exactly like Photoshop. Grip at the very top undocks it into a floating `QDockWidget` with a vertical title.
- Right dock default stack, top to bottom:
  - Color / Swatches / Gradients (tabbed)
  - Adjustments / Styles (tabbed)
  - Layers / Channels / Paths (tabbed), Layers active
  - Properties (own group under Layers, or tabbed with Layers if width is tight — default is its own group)
- Panel tabs: 11 px semibold, 24 px tab height, active tab brighter text, inactive `#B8B8B8`.
- Each panel has a hamburger menu at the top-right.
- Panels collapse to icons (24 px rail) when dragged to the edge; Photoshop’s icon-mode dock must work.
- Document tabs: 28 px, active tab `#454545`, inactive `#323232`, close button on hover, middle-click closes, drag to reorder, drag out to a new window.
- Canvas surround (pasteboard): `#262626`. Document drop shadow 8 px, 40% black.
- Status bar inside the document window, not the application window: zoom percentage (click to open Zoom dialog), document profile, bit depth, dimensions, scratch size. Click the status area cycles info modes: Document Sizes, Document Profile, Document Dimensions, Scratch Sizes, Efficiency, Timing, Current Tool.
- Contextual Task Bar: a floating rounded bar (radius 6, `#2C2C2C`, 1 px `#4A4A4A`) that appears 12 px under the current selection or under the options bar if nothing is selected. Contents change with context (see §8).
- Home screen on launch when no document is open: left rail (Home, Learn, Your files), center recent documents as 160×120 thumbnails with name and modified time, right side “New file” card and preset sizes. Skip Home with a preference.

### 1.2 Four interface themes

Preferences → Interface → Color Theme:

| Theme    | App bg   | Panel   | Canvas surround | Text    | Border  |
|----------|----------|---------|-----------------|---------|---------|
| Darkest  | `#1E1E1E`| `#262626`| `#141414`      | `#E6E6E6`| `#111111`|
| Dark     | `#323232`| `#3A3A3A`| `#262626`      | `#E8E8E8`| `#1E1E1E`|
| Light    | `#B8B8B8`| `#C8C8C8`| `#A0A0A0`      | `#1A1A1A`| `#8E8E8E`|
| Lightest | `#F0F0F0`| `#F7F7F7`| `#D6D6D6`      | `#1A1A1A`| `#C4C4C4`|

Default is Dark. Accent `#E8893A` on focus rings, active tool well, slider fills, and the workspace-switcher underline. Selection marching ants are the classic 1 px black/white animated dash (8 px period, 150 ms).

### 1.3 Workspaces

Ship these and make them resettable from Window → Workspace:

- Essentials (default)
- Photography (Histogram, Adjustments, Properties, Layers; toolbar photography subset)
- Painting (Brushes, Color, Swatches, Layers, Paths)
- Graphic and Web (Character, Paragraph, Layers, Properties)
- Motion (Timeline docked at bottom)

Workspace files are JSON in the user config directory. Reset Essentials restores the layout in §1.1.

### 1.4 Screen modes

Cycle with `F`:

1. Standard (menus, options, docks, document frame)
2. Full Screen With Menu Bar
3. Full Screen (canvas only; Tab still hides panels)

`Tab` hides all panels. `Shift+Tab` hides docks but keeps toolbar and options bar.

---

## 2. Legal and engineering boundaries

- Clean-room. Study observable behavior and the public Adobe Photoshop File Formats Specification only. Do not paste source from Photopea, GIMP, Krita, PhotoTux, PhotoCraft, or any other editor.
- Do not ship Adobe icons, cursors, splash art, sample images that are Adobe’s, or the word Photoshop in menus.
- Third-party libraries must be permissive or LGPL-compatible and dynamically linked where LGPL requires it: Qt 6 (LGPL), LibRaw (LGPL/CDDL), Little CMS 2 (MIT), libpng, libjpeg-turbo, libtiff, libwebp, libavif, libjxl, OpenEXR, libheif where the patent grant allows, zlib, zstd.
- PSD implementation is written against the public spec (File Header, Color Mode Data, Image Resources, Layer and Mask Information, Image Data). Document every unsupported tagged block in an import report rather than silently dropping it.

---

## 3. Platform matrix — all of these must build

Qt 6.8 or newer. Widgets, not QML, so the app is native (native menus, native file dialogs, native window frames, native drag-and-drop, native IME). OpenGL or Vulkan viewport via `QRhi` / `QOpenGLWidget` with a CPU raster fallback.

### Desktop OS

- Windows 10 21H2 x64, Windows 11 x64 and ARM64
- macOS 12 Monterey through current, Intel and Apple Silicon, universal binary
- Linux x86_64 and aarch64:
  - Ubuntu 22.04, 24.04, 26.04
  - Debian 12, 13
  - Linux Mint 21, 22
  - Pop!_OS 22.04
  - Fedora 40, 41, 42
  - Arch Linux (rolling; document the dependency list)
  - Manjaro
  - openSUSE Leap 15.6 and Tumbleweed
  - elementary OS 7 / 8
  - Zorin OS 17
  - NixOS (provide a `flake.nix` or a documented `shell.nix`)
  - Alpine (musl build optional, behind a CMake flag)
- FreeBSD 14 (best-effort, same Qt stack)

### Session / display

- Windows: Win32 + DWM, per-monitor DPI v2, 100–300%
- macOS: Cocoa, Retina 1x/2x, native color space tagging (Display P3 aware)
- Linux: X11 and Wayland. On Wayland use `QT_QPA_PLATFORM=wayland` by default when available, with X11 fallback. Honor `XCURSOR_SIZE`. Do not require a compositor-specific protocol beyond what Qt provides.

### Packaging you must emit

- Windows: MSI via CPack + WiX, and a portable zip. Installer registers `.spe`, `.psd`, `.psb` and optional RAW associations behind a checkbox.
- macOS: signed-ready `.app` in a `.dmg`, `Info.plist` document types, Retina icons.
- Linux: `.deb`, `.rpm`, AppImage, and a Flatpak manifest (`io.serika.PhotoEdit`). Also a `.tar.gz` with `bin/`, `lib/`, `plugins/`, `share/`.
- Desktop entry: `Serika PhotoEdit.desktop`, icon `serika-photoedit`, MIME `application/vnd.serika.photoedit`, `image/vnd.adobe.photoshop`, RAW MIME types.

### Native integration per OS

- Windows: jump list recent files, taskbar progress on export, `WM_COPYDATA` not required, dark title bar via DWMWA_USE_IMMERSIVE_DARK_MODE when theme is Dark/Darkest.
- macOS: application menu with About, Settings (`Cmd+,`), Hide, Quit; trackpad pinch-zoom; force-touch pressure if available mapped to brush pressure; color-sync profiles from `/Library/ColorSync`.
- Linux: XDG config `~/.config/serika-photoedit/`, data `~/.local/share/serika-photoedit/`, cache `~/.cache/serika-photoedit/`. Portals for file dialogs when `XDG_CURRENT_DESKTOP` is set and the portal is available, else Qt dialogs. Register MIME via `shared-mime-info` XML.

---

## 4. Repository layout

```
serika-photoedit/
  CMakeLists.txt
  README.md
  LICENSE
  cmake/                       # Qt, vcpkg/Conan optional, packaging
  src/
    app/                       # main.cpp, Application, HomeWindow
    ui/                        # chrome: MenuBuilder, OptionsBar, ToolBar, Docks, StatusBar
    ui/panels/                 # one class per panel
    ui/canvas/                 # CanvasView, Rulers, GuidesOverlay, MarchingAnts
    ui/dialogs/                # New, Export, Preferences, Curves, Levels, RAW
    tools/                     # one class per tool, ToolRegistry
    document/                  # Document, Layer, Mask, Channel, Path, History
    compositor/                # CPU tile compositor, blend modes, adjustment eval
    gpu/                       # optional QRhi compositor
    color/                     # CMS, profiles, bit depth conversion
    io/                        # format registry
    io/spe/                    # native container
    io/psd/                    # PSD/PSB reader and writer
    io/raw/                    # LibRaw develop
    io/raster/                 # png jpeg tiff webp avif jxl exr heif gif bmp tga
    filters/                   # filter registry
    actions/                   # action recorder / player
    platform/                  # win mac linux shims
  resources/
    icons/                     # original SVG icons
    themes/dark.qss darkest.qss light.qss lightest.qss
    presets/                   # new-doc presets, RAW defaults, workspaces
    profiles/                  # sRGB, Display P3, Adobe RGB compatible, ProPhoto, Gray, CMYK FOGRA39
  tests/
  packaging/
    windows/ macos/ linux/ flatpak/ nix/
```

Language: C++20. Qt 6 Widgets. No Python in the hot path. Warnings as errors on CI. Clang-format and clang-tidy config included.

---

## 5. Document model

A document owns:

- Width, height (1…300000 px; PSB path above 30000)
- Resolution (ppi, default 300 for print, 72 for web presets) — metadata, not pixel size
- Color mode: Bitmap, Grayscale, Indexed, RGB, CMYK, Lab, Multichannel
- Bit depth: 1, 8, 16, 32 (float for 32)
- ICC profile (embedded)
- Pixel aspect ratio
- Layer tree (groups nest arbitrarily)
- Alpha channels (extra channels beyond composite)
- Paths (pen paths, shape paths, clipping paths)
- Guides (horizontal/vertical, px or percent)
- Grid settings, ruler origin
- Selection (raster mask, full bit depth, plus optional vector component)
- History stack (default 50 states, preference 1–1000) and a snapshot list
- Annotation notes
- Comp / artboard list (artboards are a layer group with a rect and background)

### 5.1 Layer types

- Pixel layer (tiled)
- Fill layer: solid, gradient, pattern
- Adjustment layer (non-destructive, see §10)
- Type layer (HarfBuzz + FreeType via Qt; stores runs, paragraph attrs, warp)
- Shape layer (vector, fill, stroke)
- Smart object (embedded `.spe` or raster, transform stored separately; linked smart object is a path)
- Video / frame layer stub that imports the first frame of a video via Qt Multimedia and marks the rest as unsupported in the import report
- Group (folder), blend mode default Pass Through
- Artboard (group + rect + matte color)

Every layer has: name, visible, lock bits (transparent pixels, image pixels, position, all), opacity 0–100, fill 0–100, blend mode, knockout (none/shallow/deep), blend-if sliders per channel, channel exclusions, clipping mask flag, layer mask, vector mask, effects list, id (stable uint64).

### 5.2 Tiles

Internal storage is 256×256 tiles, copy-on-write. Empty tiles are not allocated. 8-bit is `uint8`, 16-bit `uint16`, 32-bit IEEE float. Compositor reads tiles; tools write dirty rects and push one history command per gesture (stroke is one command, not one command per dab — dab data lives inside the command).

### 5.3 Native `.spe`

Chunked container:

```
SPE\0  version u32  header JSON length u32  header JSON
then chunks: FOURCC, u64 length, payload, crc32
  TILE  layer-id, tile-x, tile-y, codec (raw|zstd), bytes
  MASK  same
  PATH  SVG-like path commands
  META  XMP packet
  PREV  JPEG preview 512 px
```

Atomic save: write `file.spe.tmp`, fsync, rename. Auto-recovery every 10 minutes to the cache dir, preference-controlled.

---

## 6. Compositor

CPU compositor is the oracle. GPU compositor must match it within 1/255 per 8-bit channel on a test suite.

Blend modes, in Photoshop order, with Pass Through only on groups:

Normal group: Normal, Dissolve
Darken: Darken, Multiply, Color Burn, Linear Burn, Darker Color
Lighten: Lighten, Screen, Color Dodge, Linear Dodge (Add), Lighter Color
Contrast: Overlay, Soft Light, Hard Light, Vivid Light, Linear Light, Pin Light, Hard Mix
Inversion: Difference, Exclusion, Subtract, Divide
Component: Hue, Saturation, Color, Luminosity

Formulas are the public Photoshop blend formulas (W3C compositing is not a substitute for Color Burn / Vivid Light / Hue). Implement in linear light as an option (Preferences → Performance → Blend in linear) and in gamma-encoded space by default to match Photoshop’s historical default. Document the choice in a comment next to the formula.

Layer styles, evaluated bottom to top as Photoshop does: drop shadow, inner shadow, outer glow, inner glow, bevel and emboss, satin, color overlay, gradient overlay, pattern overlay, stroke. Each has blend mode, opacity, angle, distance, size, contour.

Adjustment layers affect the composite below, clipped to their mask, and do not change pixel layers until rasterized.

---

## 7. Tools — full toolbar, Photoshop order

Single-column order. A small triangle means a flyout. Shortcut in parentheses; Shift+key cycles the flyout.

1. Move (V) — auto-select, show transform controls, align, distribute
2. Artboard (V flyout)
3. Rectangular Marquee (M), Elliptical Marquee, Single Row, Single Column
4. Lasso (L), Polygonal Lasso, Magnetic Lasso
5. Object Selection (W), Quick Selection, Magic Wand
6. Crop (C), Perspective Crop, Slice, Slice Select
7. Frame (K)
8. Eyedropper (I), 3D Material Eyedropper stub, Color Sampler, Ruler, Note, Count
9. Spot Healing (J), Healing Brush, Patch, Content-Aware Move, Red Eye
10. Brush (B), Pencil, Color Replacement, Mixer Brush
11. Clone Stamp (S), Pattern Stamp
12. History Brush (Y), Art History Brush
13. Eraser (E), Background Eraser, Magic Eraser
14. Gradient (G), Paint Bucket
15. Blur (no default key), Sharpen, Smudge
16. Dodge (O), Burn, Sponge
17. Pen (P), Freeform Pen, Curvature Pen, Add Anchor, Delete Anchor, Convert Point
18. Horizontal Type (T), Vertical Type, Horizontal Type Mask, Vertical Type Mask
19. Path Selection (A), Direct Selection
20. Rectangle (U), Ellipse, Triangle, Polygon, Line, Custom Shape
21. Hand (H)
22. Rotate View (R)
23. Zoom (Z)

Bottom of toolbar, not tools: foreground/background swatches, swap (X), default (D), Quick Mask (Q), screen-mode cycle.

Brush engine: size 1–5000, hardness, spacing, angle, roundness, flow, opacity, smoothing, transfer by pressure/tilt if the tablet is present (Qt tablet events). Brush presets saved as `.spebrush` JSON. Alt-right-drag (Win/Linux) or Ctrl+Option-drag (macOS) is the HUD size/hardness control.

Selection modifiers: Shift add, Alt/Option subtract, Shift+Alt intersect, Shift (no drag) constrain square/circle. Feather, expand, contract, smooth, border, transform selection, save/load selection to channel.

Content-aware fill: a real patch-synthesis fallback (PatchMatch-style nearest neighbor on a pyramid, CPU) so Spot Healing and Edit → Content-Aware Fill do something correct on a 2k image, not a blur smear.

---

## 8. Contextual task bar contents

- No selection, pixel layer: Remove Background, Select Subject, Convert to Smart Object
- Active selection: Mask, Adjustment, Fill, Invert, Deselect, Transform
- Type layer: font family, size, alignment, color
- Shape: fill, stroke, stroke width
- Adjustment layer: reset, clip to layer
- RAW document not yet opened as pixels: Open in Develop

Select Subject and Remove Background use an on-device fallback (grab-cut / color model) and a plugin hook `ISubjectSelector` so a later ONNX model can replace it without UI changes. Do not call a network service.

---

## 9. Panels

Implement all of these as real `QDockWidget` panels, registered in Window menu:

Layers, Channels, Paths, Properties, Adjustments, Color, Swatches, Gradients, Patterns, Styles, Brushes, Brush Settings, Character, Paragraph, Glyphs, Info, Histogram, Navigator, History, Actions, Layers Comps, Notes, Timeline (frame animation: play, fps, per-frame visibility), Libraries (local preset folders only — no cloud).

Layers panel details that must match:

- Blend mode combo and opacity field at top
- Lock buttons: transparent, image, position, all
- Fill under opacity in the panel menu’s expanded header
- Eye, thumbnail (layer or mask; click thumbnail to target mask), link icon, name
- Group disclosure triangle
- Fx badge if styles exist; click to expand effects
- Mask thumbnail to the right of layer thumbnail
- Filter row at top (kind, name, effect, attribute) like Photoshop’s layer search
- Footer buttons: link, effects, mask, adjustment, group, new layer, delete
- Right-click menu: duplicate, delete, convert to smart object, rasterize, merge down, merge visible, flatten, clipping mask, select pixels, blending options

Properties panel switches on selection: mask density/feather, adjustment controls, shape transform, type character basics, artboard size.

Info panel: two color readouts (actual, proof), x/y, width/height while dragging, tool hint.

---

## 10. Adjustments and Image menu

Non-destructive adjustment layers and destructive Image → Adjustments versions of the same operations:

Brightness/Contrast, Levels, Curves, Exposure, Vibrance, Hue/Saturation, Color Balance, Black & White, Photo Filter, Channel Mixer, Color Lookup (`.cube`), Invert, Posterize, Threshold, Gradient Map, Selective Color, Shadows/Highlights, HDR Toning, Desaturate, Match Color, Replace Color, Equalize.

Auto Tone, Auto Contrast, Auto Color.

Image → Mode conversions between the color modes in §5, with flatten warning when needed.
Image → Image Size (resample: nearest, bilinear, bicubic, bicubic smoother, bicubic sharper, Lanczos).
Image → Canvas Size, Image Rotation, Crop, Trim, Reveal All.

---

## 11. Filters

Filter menu groups:

- Last Filter (`Ctrl/Cmd+F`), Convert for Smart Filters
- Blur: Gaussian, Box, Motion, Radial, Surface, Lens Blur (simplified kernel), Smart Blur
- Blur Gallery panel for Iris, Tilt-Shift, Field Blur (at least Field and Iris)
- Distort: Spherize, Pinch, Twirl, Ripple, Wave, Polar Coordinates
- Noise: Add Noise, Despeckle, Dust & Scratches, Median, Reduce Noise
- Pixelate: Mosaic, Color Halftone, Pointillize, Crystallize, Fragment
- Render: Clouds, Difference Clouds, Lens Flare, Fibers
- Sharpen: Sharpen, Sharpen More, Sharpen Edges, Unsharp Mask, Smart Sharpen
- Stylize: Emboss, Find Edges, Oil Paint, Wind, Diffuse
- Other: High Pass, Minimum, Maximum, Offset, Custom (5×5 kernel)
- Camera Raw Filter (opens the develop dialog on a smart-object copy)

Liquify as its own dialog: warp, reconstruct, smooth, twirl, pucker, bloat, freeze mask. Mesh stored in the smart filter.

Neural Filters menu exists and lists “no on-device model installed” with the plugin hook, not a dead end.

---

## 12. File I/O — required formats

### Open

- `.spe` `.speb` full round-trip
- `.psd` `.psb` : header, resources, layers, masks, groups, blend modes, opacity, type layers as type if the engine data parses else as pixels plus a warning, smart objects as smart objects when the embedded file is a known raster, adjustment layers where the tagged block is known, layer effects where known, composite image as fallback. Import report docked as a non-modal panel listing skipped blocks.
- RAW: `.dng` `.cr2` `.cr3` `.nef` `.nrw` `.arw` `.srf` `.sr2` `.raf` `.orf` `.rw2` `.pef` `.srw` `.rwl` `.3fr` `.fff` `.iiq` `.erf` `.mef` `.mos` `.x3f` via LibRaw. Open into the Develop dialog (§13), not straight to an 8-bit bake, unless the user picks “skip develop”.
- Raster: PNG, JPEG, JPEG XL, WebP, AVIF, HEIF/HEIC (when libheif is present), TIFF (including multi-page as layers), BMP, GIF (frames as layers or timeline), TGA, OpenEXR, Radiance HDR, JPEG 2000 if JasPer/OpenJPEG is found
- SVG and PDF: import as pixels at a chosen dpi, or as a shape layer if the path count is under 2,000
- Optional: `.xcf` flattened import behind a flag (do not block the build on it)

### Save / Export As

- `.spe` always full fidelity
- `.psd` `.psb` write: composite, pixel layers, groups, masks, blend modes, opacity, text as type if the descriptor can be emitted, else rasterized type with a report
- PNG, JPEG (quality, progressive, embed profile), TIFF, WebP, AVIF, JPEG XL, BMP, TGA, GIF, OpenEXR
- Export As dialog: scale, resample, format, metadata strip, color convert to sRGB
- Quick Export PNG (preference for format)
- Generate → Image Assets stub that exports named layers (`200% logo.png`) on save

Color: embed ICC on every format that supports it. Convert using Little CMS 2, black-point compensation on by default.

---

## 13. RAW develop

Dialog modeled on Camera Raw’s basic panel, not a wall of LibRaw knobs.

Panels: Basic, Tone Curve, Detail, Color Mixer, Optics, Geometry, Calibration.

Basic: white balance (as shot, auto, temperature, tint), exposure, contrast, highlights, shadows, whites, blacks, texture, clarity, dehaze, vibrance, saturation.

Detail: luminance NR, color NR, sharpness amount/radius/detail/masking.

Optics: enable lens profile if LibRaw provides one, manual distortion, vignette, CA remove.

Output: 8-bit or 16-bit, sRGB / Display P3 / Adobe-RGB-compatible / ProPhoto, open as smart object or pixels.

Settings stored as XMP sidecar next to the RAW if the user confirms, and inside the `.spe` once opened.

---

## 14. Menus (build every item; wire every item)

File: New, Open, Browse in Bridge stub (opens the folder), Open As, Open Recent, Close, Close All, Close Others, Save, Save As, Save a Copy, Export As, Quick Export, Generate, Place Embedded, Place Linked, Package, File Info (XMP editor), Print (Qt print dialog + position/scale), Print One Copy, Exit.

Edit: Undo, Redo, Toggle Last State, Fade, Cut, Copy, Copy Merged, Paste, Paste in Place, Paste Into, Paste Outside, Fill, Stroke, Content-Aware Fill, Transform (Free, Scale, Rotate, Skew, Distort, Perspective, Warp, Flip), Puppet Warp (mesh deform, minimum viable), Free Transform, Auto-Align (stub with translation-only), Define Brush, Define Pattern, Purge, Color Settings, Keyboard Shortcuts, Menus, Preferences.

Image, Layer, Type, Select, Filter, 3D (menu present, items report “3D workspace not in 1.0” except New 3D Extrusion disabled), View, Window, Help.

Type: panels plus anti-alias, orientation, convert to shape, convert to point text, warp text.

Select: All, Deselect, Reselect, Inverse, All Layers, Subject, Color Range, Focus Area, Select and Mask workspace (view modes: onion skin, overlay, black, white; radius, smooth, feather, contrast, shift edge), Modify, Grow, Similar, Transform Selection, Load/Save Selection, New Selection from Layer.

View: Proof Colors, Gamut Warning, Zoom In/Out, Fit on Screen, 100%, 200%, Flip Horizontal view, Rulers, Guides, Grid, Snap, Snap To, Lock Guides, Extras, Show (selection, path, grid, guides, notes).

---

## 15. Shortcuts

Match Photoshop defaults. Modifier map: Ctrl on Windows/Linux, Cmd on macOS; Alt on Windows/Linux, Option on macOS. Implement both in one table.

Tools: V M L W C I J B S Y E G O P T A U H R Z, X swap colors, D default colors, Q quick mask, F screen mode, Tab panels, Space temporary hand, Ctrl/Cmd temporary move while a paint tool is up.

Edit: Ctrl+Z undo, Ctrl+Shift+Z redo, Ctrl+Alt+Z step back, Ctrl+T free transform, Ctrl+C/V/X, Ctrl+Shift+C copy merged, Ctrl+Shift+V paste in place, Ctrl+Enter commit, Esc cancel.

Layers: Ctrl+Shift+N new layer, Ctrl+Alt+Shift+N new layer no dialog, Ctrl+J duplicate, Ctrl+Shift+J cut to new layer, Ctrl+G group, Ctrl+Shift+G ungroup, Ctrl+E merge down, Ctrl+Shift+E merge visible, Ctrl+Alt+Shift+E stamp visible, Ctrl+Shift+F flatten, Ctrl+] / [ order, Ctrl+Shift+] / [ front/back, Ctrl+Alt+G clipping mask.

Select: Ctrl+A, Ctrl+D, Ctrl+Shift+D, Ctrl+Shift+I, Ctrl+Alt+A all layers.

View: Ctrl++ / Ctrl+-, Ctrl+0 fit, Ctrl+1 actual pixels, Ctrl+R rulers, Ctrl+; guides, Ctrl+’ grid.

Adjustments: Ctrl+L levels, Ctrl+M curves, Ctrl+U hue/saturation, Ctrl+B color balance, Ctrl+I invert, Ctrl+Shift+U desaturate, Ctrl+Shift+L auto tone, Ctrl+Shift+B auto color, Ctrl+Alt+Shift+B black and white.

Blend modes while a layer is targeted: Shift+Alt+N normal, M multiply, S screen, O overlay, F soft light, H hard light, and the rest of the public shortcut set. Shift+plus / Shift+minus cycles.

File: Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S, Ctrl+W, Ctrl+Q, Ctrl+P, Ctrl+Alt+Shift+S export as.

The shortcut editor (Edit → Keyboard Shortcuts) writes JSON over the defaults.

---

## 16. Preferences

Pages: General, Interface, Workspace, Tools, History, File Handling, Export, Performance, Cursors, Transparency & Gamut, Units & Rulers, Guides Grid Slices, Plug-ins, Technology Previews.

Performance: memory usage percent, history states, cache tile size, cache levels, GPU on/off, blend in linear. Scratch disk picker, default the user cache dir.

Cursors: standard / precise / brush-size preview, with crosshair in brush tip.

---

## 17. Actions, batch, scripting

- Actions panel: record, play, insert stop, insert menu item, save `.speaction` set.
- File → Automate → Batch runs an action on a folder, with open/save overrides.
- A small command palette (`Ctrl+Shift+P` — extra, does not replace Photoshop shortcuts) lists every menu command.
- Headless `serika-photoedit --batch action.json in out` for CI.

---

## 18. Visual design tokens (implement as a theme, not hardcoded per widget)

Dark theme QSS variables:

- `--bg: #323232`
- `--panel: #3A3A3A`
- `--panel-header: #3A3A3A`
- `--canvas: #262626`
- `--input: #2A2A2A`
- `--border: #1E1E1E`
- `--text: #E8E8E8`
- `--text-dim: #B0B0B0`
- `--accent: #E8893A`
- `--accent-hover: #F0A15C`
- `--danger: #E34850`
- `--selection: rgba(232,137,58,0.35)`

Controls: 4 px radius on inputs and buttons, 0 px on panel frames, 22 px button height in options bar, sliders 12 px hit area with 2 px track. No giant padding. Density should feel like Photoshop, not a marketing site. Icons 18 px in the toolbar, optical padding so the column is 38 px.

Home screen and About may show a small original fox-ear mark in accent amber. Do not use copyrighted anime stills.

---

## 19. Performance budgets

- 4000×3000, 10 pixel layers, zoom and pan at display refresh on a 2020 integrated GPU, or 30 fps CPU fallback.
- Brush stroke on a 4k 8-bit layer: dab latency under 16 ms after the first tile is hot.
- Open a 200 MB PSD with 20 layers in under 5 s on SSD, report progress.
- Undo of a brush stroke is O(dirty tiles), not a full-document copy.

---

## 20. Tests you must add and run

- Blend mode oracle: 27 modes, 8-bit and 16-bit, known vectors.
- PSD round-trip: generate a layered file, write, read, compare pixels and stack.
- SPE round-trip including mask and text.
- RAW: if a CC0 DNG is not in-repo, synthesize a minimal DNG and run it through LibRaw.
- Selection boolean ops.
- History coalescing.
- Theme snapshot test is optional; a layout assertion that Essentials creates the four docks is required.

---

## 21. How you work in Ultra mode

- Create the repo and keep it building after every major slice.
- Do not output a design-only answer. Code is the deliverable.
- Prefer complete simple implementations over abstract frameworks. A 200-line Gaussian blur that works beats a filter-graph manifesto.
- UI strings in English. Layout direction LTR. Use `QLocale` for numbers.
- Commit-sized comments only where a formula is non-obvious.
- When you finish, print: platforms configured, formats implemented, tools implemented, tests passed, and the exact command to launch.

## 22. Acceptance checklist

The build is not done until all of these are true:

- [ ] App launches to the Home screen, then New Document opens a canvas with the chrome in §1.1
- [ ] Dark theme matches the tokens; toolbar flyouts and two-column mode work
- [ ] Brush, eraser, move, marquee, lasso, crop, type, eyedropper, clone stamp, gradient all change pixels or vectors and undo
- [ ] Layers panel: add, delete, reorder, group, mask, opacity, all 27 blend modes, clipping mask
- [ ] Levels, Curves, Hue/Saturation as adjustment layers
- [ ] Save and reopen `.spe` losslessly
- [ ] Open and save `.psd` with layers
- [ ] Open a RAW into the Develop dialog and land 16-bit pixels in a document
- [ ] Open PNG JPEG TIFF WebP and export them
- [ ] Shortcuts in §15 work on the host OS
- [ ] CPack or the platform script for the host OS produces an installable artifact
- [ ] README lists every OS in §3 and the package command for each

Begin.
