# Implementation status

This repository contains a working native C++20 / Qt 6 Widgets image editor. It is
not a complete implementation of every requirement in the supplied master brief,
nor a claim of full compatibility with another editor. This inventory describes
the current code and its practical limits. Build, package, launch commands and the
latest integrated test results are in [README.md](../README.md).

“Implemented” below means there is working code for the stated behavior.
“Partial” means useful behavior exists with the specific limitations described.
“Configured, untested” means build or packaging configuration exists; it does not
mean that a binary was built, installed or exercised on that platform.

## Platforms and distribution

| Platform from the brief | Current evidence |
| --- | --- |
| Windows x64 | Windows 11 host build with Qt 6.8.3/MinGW and native application/package checks; Windows Server 2022 CI build and offscreen tests passed. Package/installer scope is recorded in the README. |
| Windows 10 21H2 / Windows 11 version matrix | Windows source and package configuration present; separate OS-version compatibility runs have not been performed. |
| Windows ARM64 | CMake configuration is architecture independent; no ARM64 build or device run performed. |
| macOS 12 through current, Intel / Apple Silicon | Earlier macOS 14 CI build/offscreen tests passed. Native Intel/macOS 15 and Apple Silicon/macOS 14 DMG/ZIP validation is pending. Other versions, full interactive acceptance, universal binaries, Developer ID signing/notarization and installation are unverified. |
| Ubuntu 22.04 / 24.04 / 26.04; Debian 12 / 13; Mint 21 / 22; Pop!_OS 22.04 | Earlier Ubuntu 24.04 x86_64 build/offscreen tests passed. Native Ubuntu 24.04 x86_64/aarch64 tar.gz, AppImage and DEB validation is pending. glibc 2.39 is the package baseline; other listed distributions and normal installation are unverified. |
| Fedora 40 / 41 / 42; openSUSE Leap 15.6 / Tumbleweed | RPM/TGZ configuration present; configured, untested. |
| Arch / Manjaro; elementary OS 7 / 8; Zorin OS 17 | Generic Qt/CMake Linux build path documented; configured, untested. |
| NixOS | Nix flake present; not evaluated or built on NixOS. |
| Alpine / musl | Optional CMake flag and dependency guidance present; no musl build performed. |
| Linux aarch64 | Native Ubuntu 24.04 ARM64 runner, Qt 6.8.3 kit and release packaging configured; package validation pending. |
| FreeBSD 14 | Intended portable Qt/CMake path; no build or runtime validation performed. |

Windows has CPack ZIP/WiX configuration and optional association component sources.
Linux has DEB/RPM/TGZ configuration, an AppImage helper, Flatpak manifest, desktop
entry and MIME XML. These files are not evidence of completed packages for every
OS. Platform-specific dependencies and deployment tools must be available on the
machine producing each package.

[Earlier CI](https://github.com/serika-dev/SerikaPhotoEdit/actions/runs/37781612875) passed builds, offscreen tests, runtime staging and artifact uploads on Windows Server 2022, macOS 14 and Ubuntu 24.04. The additional Unix package jobs are pending; their [validation record](RELEASE-VALIDATION.md) will distinguish automated package checks from full interactive desktop acceptance. Application source remains the immutable v0.0.1 tag; current packaging scripts overlay updated documentation and dependency notices.

The viewport and compositor are CPU raster implementations. There is no QRhi,
OpenGL or Vulkan compositor. Qt supplies the windowing, native dialog, IME and
high-DPI infrastructure. Windows sets a DWM dark title bar. Jump lists, taskbar
export progress, theme-aware DWM updates, macOS force-touch/ColorSync integration,
and Linux portal/session behavior have not been implemented or verified as a
complete native-integration matrix. Qt's standard paths are used rather than a
fully customized XDG directory layout.

## Application chrome and panels

Implemented chrome includes Home and recent-file cards, new-document presets,
document tabs, native menu widgets, original SVG tool icons, toolbar flyouts,
one/two-column toolbar, tool options, four themes, five workspace presets,
workspace reset/save, dock visibility, screen modes and a command palette.
Document tabs reorder, close on middle click, and can move to another window by
the context menu or release outside the window. Drag-out behavior is implemented
but still needs manual multi-window/platform testing.

Essentials arranges four separate primary right-side dock groups: Color,
Adjustments, Layers and Properties, with companion panels tabbed into the first
three. The layout test checks visible, vertically distinct geometries after a
document opens, rather than merely checking that dock objects exist. Editing
chrome is hidden while Home has no open document. This is an original interface
with the requested general layout; exact geometry and interaction parity across
all display scales have not been established.

| Panel or interaction | Behavior and limitations |
| --- | --- |
| Layers | Stable IDs, hierarchy, thumbnails, visibility, blend/opacity/fill, locks, raster/vector masks, clipping, layer operations, effects and Smart Filters. Mask thumbnails support load-selection modifiers, preview and disabling; masks have linked/unlinked positions. Complete kind/name/effect filtering and linked-layer workflows remain limited. |
| Properties / Adjustments | Context-dependent fields, parameterized adjustments, mask density/feather and editable Smart Filter stack. Some advanced operations expose a reduced set of controls. |
| Color / Histogram / Navigator / Info | Functional color selection, histogram, navigation and canvas information. Info does not provide a separately color-managed proof readout. |
| History | Executed-state list, undo/redo, purge and selectable History Brush source. History Brush restores native color and transparency from an available history state. There is no separate persistent snapshot browser. |
| Channels / Paths | Basic lists and selection/path operations. Saved selections live in metadata; there is no independent full-depth extra-channel compositing model or complete clipping-path manager. |
| Character / Paragraph / Glyphs | Font/size, text editing and selected glyph insertion. They are lightweight controls, not a complete typography system. |
| Swatches / Gradients / Patterns / Styles / Brushes | Usable generated choices and basic brush/pattern definition. There is no full preset import/export, search, migration, organized library or brush-tip engine. |
| Brush Settings | Basic shared brush controls. Advanced brush dynamics, textured tips, dual brush and a complete preset editor are absent. |
| Actions | Record supported editing commands, play and save/load parameterized action sets. See the action limitations below. |
| Notes | Document metadata text editor. |
| Layer Comps | Persistent records capture/restore visibility, position and appearance, with deletion and undo. Native files retain comps. There is no complete comp recapture/duplicate/export manager. |
| Timeline | Plays layers in sequence at a chosen FPS. No editable per-frame visibility program, frame duplication/export manager or video import exists. |
| Libraries | Local preset-folder browser. It is not an asset catalog or cloud service. |
| Collapsed panels | A narrow rail is implemented by hiding panel contents. A full icon rail with temporary panel flyouts and edge-drag interactions is absent. |
| Contextual task bar | Floating canvas child with selection/subject/background actions. It remains near the top of the canvas; it does not follow selection bounds or provide every type/shape/adjustment/RAW context from the brief. |
| Document status | Zoom, dimensions, depth and profile indication. The requested scratch/efficiency/timing information cycle is absent. |

The shortcut registry has stable IDs and command, tool, cycling, hold, brush,
layer and crop scopes. Defaults cover common file/edit/layer/selection/view
commands, tool groups and Shift cycling, temporary Hand/Move, color swapping,
quick mask, blend modes, brush size/hardness, opacity/flow digits, layer
ordering/navigation, crop overlays and commit/cancel. Bindings can be changed,
disabled, searched, reset and imported/exported as JSON. Conflict checks include
multi-key prefixes, and text fields retain their editing keys. Remapping replaces
the previous bindings, including native canvas keys. Tests exercise the table,
conflicts, persistence, text-field protection, hold release and real editor
workflows. macOS command keys and non-US physical layouts need device validation.
Menus uses the shortcut editor rather than separate menu-visibility customization.

## Document model, history and precision

The model supports raster, group, artboard, text, shape, adjustment, solid-fill,
gradient-fill and raster-backed smart-object layers. Groups can nest, IDs remain
unique after import, and artboards have clipping rectangles and matte colors.
There is no separate pattern-fill layer or video/frame layer model.

Raster storage consists of sparse 256×256 copy-on-write `QImage` tiles. Empty tiles
are omitted. Native storage supports 8-bit RGBA, 16-bit RGBA and 32-bit floating
RGBA. Native saves retain exact samples, including floating samples above 1.
Regional tile reads preserve 16-bit sample bytes and alpha. Partially allocated
edge tiles can grow when a canvas expands.

Transactions coalesce a gesture into one history entry, support cancellation, and
share unchanged tile images. Dirty state follows revision identity: returning to
a saved revision becomes clean, while a new branch remains dirty. Undo/redo,
history limits and history purge are implemented. The layer/tile maps are still
part of state snapshots; this is shared tile storage, not a dedicated on-disk
dirty-tile command log.

Selections and raster masks use native **8-bit, 16-bit and 32-bit floating
coverage** matching the document. Boolean operations, invert, reselect, bounds
caching, quick-mask painting and selection clipping work. Saved selections retain
native samples. Layer masks support density, feather, enabled state, targeting,
linked/unlinked offsets and apply/delete/invert. Vector masks retain editable
paths with independent density, feather, enable and linking fields. Raster and
vector coverage compose together. Painting retains untouched sub-8-bit samples;
subject analysis and some previews originate from 8-bit images. Separate vector
selections, blend-if sliders, knockout and channel-exclusion fields remain absent.

Guides, ICC bytes, resolution, color-mode label and JSON metadata are saved.
Metadata holds notes, saved selections, layer comps, grid/develop settings and retained
import blocks. Pixel aspect ratio, independent alpha channels, complete ruler
origin/grid configuration and persistent history snapshots are not dedicated
model fields.

Canvas creation enforces an 80-million-pixel safety limit. The model has sparse
tiles, but many operations and compositing allocate full images; it is not an
out-of-core 300,000-pixel editor. The Image/Canvas Size dialogs currently cap each
dimension at 30,000 and offer Qt nearest/smooth scaling. Separate bilinear,
bicubic smoother/sharper and Lanczos implementations are absent, regardless of the
current smooth option's UI label. Crop, canvas resize, image resize, trim and reveal
all are implemented. Ordinary crop can preserve outside tiles or delete cropped
pixels; preview/cancel does not mutate the document. Matrix transforms preserve
native Text/Shape/Smart Object state. Perspective crop currently rasterizes type
and reports that change.

## Compositor, adjustments and layer effects

All 27 requested layer blend modes have CPU implementations. Pass Through is
available for groups. Composition includes masks, clipping stacks, nested group
isolation/pass-through, inherited offsets, layer opacity/fill and artboard bounds.
Semitransparent clipping bases preserve their base coverage. Gamma-encoded blend
is the default; optional linear blending uses the sRGB transfer functions, not a
general per-ICC-profile scene-linear pipeline.

Core tests contain known blend vectors at both 8 and 16 bits, group/mask/clipping
regressions, alpha precision and linear-option checks. Those vectors do not prove
pixel-for-pixel equivalence for every possible color, alpha, profile and advanced
blend configuration in another application.

Levels, Curves and Hue/Saturation work as adjustment layers and destructive
operations. Curves evaluates supplied control points with monotone interpolation;
the UI offers simplified controls rather than a complete draggable curve editor.
Brightness/Contrast, Exposure, Vibrance, Color Balance, Black & White, Photo Filter,
Channel Mixer, Color Lookup, Invert, Posterize, Threshold, Gradient Map, Selective
Color, Shadows/Highlights, HDR Toning, Desaturate, Match Color, Replace Color,
Equalize and Auto Tone/Contrast/Color also have CPU implementations. Color Lookup
evaluates supplied `.cube` data with trilinear interpolation.

Advanced adjustment behavior is simplified: Shadows/Highlights and HDR Toning are
global tone operations, Selective Color uses soft hue ranges, and Match Color uses
basic statistics rather than a complete source-image workflow. Black & White has
six color-range mixers. These are useful mathematical operations without a claim
of proprietary algorithm parity. Adjustment evaluation currently clamps RGB to
0…1; **32-bit storage and HDR import do not imply a complete unclipped HDR editing
pipeline**.

All ten named styles render: drop/inner shadow, outer/inner glow, bevel/emboss,
satin, color/gradient/pattern overlay and stroke. Stroke uses a distance transform
and Fill 0 can retain effects. Effects support basic size/color/opacity and some
blend/angle/distance parameters. The dialog exposes a reduced subset, and contour,
texture, advanced bevel lighting, complete gradient/pattern presets and exact
style ordering/interactions are not fully modeled. Pattern overlay has a basic
checkerboard fallback. Effects are approximate CPU renderings.

Qt shapes and text render as editable native layers. Type currently stores one
font/color run with basic alignment/orientation. Rich runs, complete paragraph
attributes, kerning controls, text-on-path, real text warp and Adobe type-engine
descriptors are absent. Type-to-shape is available. Smart objects store raster
pixels and an optional linked path. Original pixels are retained during matrix
transforms, image resizing and Smart Filter edits, with reevaluated transform and
filter records. An editable embedded `.spe` subdocument and comprehensive linked
source reload manager remain absent.

## Canvas tools

| Tool family | Implemented behavior and limits |
| --- | --- |
| Move / Artboard | Layer movement, auto-select, transform-control display and basic artboard creation. Advanced multi-layer alignment/distribution needs further work. |
| Marquee / Lasso / selection tools | Rectangle, ellipse, row/column, free lasso, polygonal/magnetic lasso, wand, quick/object selections and selection boolean modifiers. Object/Subject selection uses a local color model, not semantic object recognition. |
| Brush / Pencil / Erasers | Native selection/mask clipping, one gesture per undo, hardness/opacity/flow, spacing, angle/roundness, smoothing, HUD sizing, brush blends including Behind/Clear and Shift straight strokes. Alt samples without painting; temporary Move/Hand preserves the selected tool. Tablet pressure/tilt paths need physical device validation. |
| Clone / Pattern / History | Clone sampling, patterns and chosen History-state painting, including native channels and transparency. Art History Brush is a simplified variation without a complete artistic stroke engine. |
| Healing / Content-Aware | Spot Healing and fill use seeded exemplar patch searches. Healing Brush corrects cloned mean color. Patch drags an explicit donor into the selection with color adaptation. Content-Aware Move transports selected pixels, fills the source hole and moves the selection in one undo. These CPU algorithms do not claim proprietary algorithm parity. |
| Color / tonal paint tools | Color replacement, mixer, blur, sharpen, smudge, dodge, burn, sponge and red-eye use CPU pixel operations. |
| Crop / Perspective Crop | Persistent dimmed preview, draggable corners/edges/body, ratios, output dimensions/DPI, swap/reset, optional pixel deletion, horizon-line straighten and automatic transparent/uniform-border crop and line-based straighten. Thirds/grid/diagonal/triangle/golden-ratio overlays have a remappable cycle key. Enter commits; Escape cancels without edits. Perspective crop previews four movable corners before projective resampling and reports type rasterization. Golden spiral, content-aware border expansion and semantic framing are absent. |
| Type / Shapes / Pen | Editable type, geometric vector shapes, pen paths, cubic Curvature Pen and direct anchor/control editing. Freeform Pen remains polygonal; Type Mask behaves as a type layer; Frame is geometry rather than a placed-image frame container. |
| Gradient / Bucket | Real fills respecting current selection. Complete multi-stop preset editing is not provided. |
| Eyedropper / sampling | Actual canvas sampling; the 3D eyedropper uses ordinary 2D sampling. Advanced sampler/proof measurement workflows are limited. |
| Hand / Rotate / Zoom | Pan, rotation/flip, zoom, fit-document/fit-selection, rulers, grid, guides and animated selection outline. Extras visibility preserves grid/guide preferences. The complete snapping-target system remains absent. |

Subject/background actions expose an `ISubjectSelector` registration hook and use a
local border-color/codebook flood fallback when no plugin is registered. No image
is sent to a network service. Focus Area currently shares the subject fallback;
it does not perform a separate focus-analysis algorithm. Select and Mask has a
dedicated preview dialog with overlay, black, white, monochrome, layer and onion
views, native add/subtract brush, edge/smart radius, smoothing, feather, contrast,
shift-edge, local color decontamination and selection/mask/new-layer outputs.
These local algorithms do not provide trained semantic hair/fur reconstruction
or every interaction of a commercial masking workspace.

Free Transform has an inline nonmutating canvas preview with corner/edge handles,
body movement, pivot, outside rotation, aspect/center modifiers and Enter/Escape
commit/cancel. Skew uses an affine shear, and Distort/Perspective use real
projective matrices. Selected raster masks transform independently;
linked masks follow their content and unlinked masks retain document position.
Text, shapes and Smart Objects retain their source and transform records. Group,
artboard and full multilayer transforms remain limited. Warp and Puppet Warp
still use the Liquify displacement editor rather than a stored pin/mesh system.
Liquify has warp, reconstruct, smooth, twirl, pucker,
bloat, freeze and thaw on a coarse inverse displacement mesh, with bilinear native
16/32-bit output and undo. The mesh is not stored as a replayable smart filter.

## Formats and color management

Detailed format behavior is maintained in [src/io/README.md](../src/io/README.md).

| Format | Current implementation |
| --- | --- |
| SPE / SPEB | Same versioned, streamed chunk container; CRC-32, checked header/end marker, preview, atomic `QSaveFile` replacement after handle flush. Current model fields round-trip, including exact native tiles, text/path/group/mask/effects/selection/guides/ICC/metadata. SPEB is not a separate disk-backed large-document engine. |
| PSD / PSB import | Independently implemented RGB/gray/duotone/CMYK/Lab 8/16/32 input, raw/PackBits and zlib ZIP/prediction, layers/groups/masks/positions/opacity/fill/blends/guides/ICC/resources. CMYK/Lab convert approximately to editable RGB; the original profile is retained. Only Invert, legacy Brightness/Contrast, Posterize and Threshold Adobe adjustment blocks are parsed. Unsupported blocks are reported and unknown blocks/resources retained as base64 metadata. |
| PSD / PSB export | Standard RGB raster composite/layers/groups/masks/blends and selected resources. Editable Serika text/shape/adjustment/style/smart-object state is written in private `sPEd` blocks with compatibility pixels. Serika reopens its own editable state; other editors see raster fallbacks. Native Adobe text, effect and embedded-smart-object descriptors are not emitted or fully parsed. |
| RAW | Genuine 16-bit decoding through dynamically linked LibRaw, camera/source metadata, pending-Develop UI and saved settings. A synthesized DNG exercises the actual decoder. Bundled Windows LibRaw enables DNG deflate; optional lossy-DNG/Foveon codecs are unavailable in that build. Camera-by-camera format coverage has not been exhaustively tested. |
| PNG / JPEG / TIFF / WebP / BMP / GIF | Installed Qt image plugins; runtime-supported formats are advertised. TIFF pages and GIF frames become layers; GIF timings are retained as metadata. Layered GIF import is not a complete animation editor/exporter. |
| TGA / Radiance HDR | Direct TGA raw/RLE truecolor/grayscale/indexed codecs and floating RGBE HDR raw/legacy/RLE codecs. HDR samples above 1 survive format/native round-trip; adjustment limits still apply. |
| SVG / PDF | Pixel import. Qt PDF renders all pages to layers at a specified DPI, default 150, when Qt Pdf is present. SVG vector import and the requested low-path-count shape conversion are absent. |
| JPEG XL / AVIF / HEIF / EXR / JPEG 2000 | Available only if compatible Qt codec plugins are installed. They are not bundled by default and are not claimed as tested formats. |
| XCF / video | No XCF or Qt Multimedia video import path. |

The PSD reader currently limits the whole-file working buffer to 2 GB and each
plane to 512 MB. Native files stream chunks but still require enough RAM for
their editable state and full-image operations. Extremely large PSB/SPEB workflows
from the brief are not validated.

ICC bytes are retained and embedded where the writer supports them. Qt
`QColorSpace` performs the implemented RGB profile conversions, including Develop
sRGB, Display P3, Adobe RGB compatible and ProPhoto RGB output. There is no
dedicated Little CMS 2 pipeline with explicit black-point compensation, complete
CMYK/Lab editing, soft proof or gamut warning. Bitmap/Grayscale/Indexed menu paths
use RGB effects while storage remains RGBA; they are not native indexed/1-bit
document modes. Profile assignment does not by itself convert existing samples.

Export has format/quality/scale/profile/metadata controls. Print uses Qt's printer
dialog and raster output. Generate Image Assets is a one-shot named-layer PNG
export; filename scale recipes such as `200% logo.png` and automatic export on
save are absent. Package copies the native document and linked files; it is not a
complete archive/dependency relinking manager. File Info edits JSON metadata,
not a general XMP packet. Recovery files are written periodically when enabled;
automatic crash-recovery discovery/restoration still needs implementation and
end-to-end validation.

## Filters, Develop, actions and preferences

All named CPU filter menu operations have image-processing implementations:
blur/blur-gallery kernels, distortions, noise, pixelation, procedural render,
sharpen, stylize and Other filters. Float working buffers preserve requested
output storage precision. These are simplified mathematical algorithms; Blur
Gallery lacks the requested interactive pin workspace and proprietary filter
appearance is not reproduced. Large-radius/filter performance is workload
dependent.

Develop has preview and Basic, Curve, Detail, Color, Optics and Geometry tabs, plus
8/16-bit output, four RGB profiles and optional confirmed XMP sidecar writing.
Implemented controls include tone/exposure/temperature/tint, noise reduction,
sharpening, gamma/hue, vignette/distortion and rotation. It lacks a complete
calibration panel, per-hue mixer, camera/lens-specific calibration profiles,
chromatic-aberration correction and the full detail/masking workflow. Develop
settings are saved in the document. “Open as smart object” creates a raster-backed
smart-object layer rather than an embedded re-developable RAW object.

Smart Filters retain original Smart Object pixels and reevaluate an ordered saved
stack at render time. Parameter edits, enable/disable, opacity, blend mode,
reordering and deletion have preview and undo. Native files preserve the stack;
Serika PSD blocks retain it alongside compatibility pixels. Independent per-filter
masks, arbitrary dependency graphs and a complete external plugin filter system
remain absent. Neural Filters
reports that no model is installed; the subject-selector hook is available, but
a general neural-filter model loader/registry is absent.

Actions store command names and parameters, play through a UI-independent runner,
save/load `.speaction` JSON and roll back the whole action when a step fails.
The headless `--batch action.json input-folder output-folder` path opens actual
documents, runs supported steps and writes native output with failure exit codes.
Unsupported steps fail explicitly. Recording does not capture arbitrary mouse
gestures or every menu item; insert-stop/menu-item editing, complete open/save
overrides and a general scripting API are absent. Fade blends the immediately
preceding pixel effect against its before-state using the requested amount in
Normal mode, retains layer opacity, and creates one undo step. Effects that change
dimensions and non-pixel edits cannot be faded. Auto-Align is a translation-only
centering operation, not image registration.

Preference pages exist, with functional theme, Home, recovery, history, export,
linear blending and selected unit/grid settings. Several pages are lightweight
information controls. Memory percentage and scratch-folder controls do not create
an enforced memory budget or disk-backed tile cache; there is no GPU or adjustable
cache hierarchy. There is no complete plug-in discovery/installation system.
Unimplemented commands report their status instead of silently succeeding.

## Verification and remaining acceptance work

The Windows engine, I/O, actions and canvas suites have passed during development.
The final integrated run, including the revised UI/layout tests, is documented in
the README with its actual results; this inventory does not substitute for those
logs. Test totals include data-driven rows and QtTest lifecycle slots where Qt
reports them, and should not be interpreted as a count of complete features.

| Automated source | Scope |
| --- | --- |
| [core_tests.cpp](../tests/core_tests.cpp) | 27 blend vectors in 8/16 bits; sparse/COW/exact tiles; masks/clipping/group/artboard behavior; transaction/cancel/history/saved revisions; selections; selected adjustments; text/shapes; resizing; composite cache regressions. |
| [io_tests.cpp](../tests/io_tests.cpp) | Native full-state/float round-trip and corruption; PSD/PSB precision/compression/masks/groups/unknown tags; raster/TGA/HDR; multipage/frame/PDF imports; genuine synthetic-DNG decode; Develop/filter properties. |
| [canvas_tests.cpp](../tests/canvas_tests.cpp) | Native 16/float mask paint, strokes/blends/modifiers, chosen History Brush source/transparency, actual Patch/Content-Aware Move transport, subject hook, paths, preview/cancel/undo crop, auto bounds/straighten, projective crop, inline transforms with linked/unlinked/mask-only/Smart Object behavior and Liquify. |
| [actions_tests.cpp](../tests/actions_tests.cpp) | Parameter replay, selection-aware fill, rollback, transform, headless file processing, malformed actions and transparent 16-bit color. |
| [ui_tests.cpp](../tests/ui_tests.cpp) | Home/chrome, four distinct Essentials dock geometries, accepted New Document dialog, live canvas, actual brush mouse gesture plus keyboard undo/redo, visibility/reorder and primary action registration. |
| [shortcuts_tests.cpp](../tests/shortcuts_tests.cpp) | Stable IDs/default table, conflicts, JSON persistence, text inputs, remapping, multi-key chords, tool/crop scopes, blend/opacity/flow and press/release holds. |
| [mask_tests.cpp](../tests/mask_tests.cpp) | Native coverage precision, density/feather, vector/raster composition, linking, saved selections, comps, Smart Filters and retained history pixels. |
| [workflow_tests.cpp](../tests/workflow_tests.cpp) | Actual command workflows, native transforms/mask targeting, crop/refinement/Smart Filter dialogs, clipboard and document-state operations. |
| [performance_bench.cpp](../tests/performance_bench.cpp) | Standalone 4000×3000 ten-layer cold composite, cached access, zoom resampling, brush dirty-region redraw and undo timings; JSON output. It measures current behavior and does not enforce unverified brief latency claims. |

The raster compositor cache supports dirty-tile regional recomposition during
unchanged pixel-stack paint transactions, with a full-composite fallback for
global/property changes, groups, effects and adjustment stacks. Undo restores
shared state quickly, but a following redraw can still require full composition.
There is no established claim of 30 FPS on a 2020 integrated GPU, sub-16-ms hot
brush latency for all stacks, or a 200 MB / 20-layer PSD open under five seconds.
Those acceptance budgets require controlled hardware benchmarks and profiling.

Manual acceptance still needs: the complete host shortcut matrix; every advanced
dialog/tool variant; tablet pressure/tilt devices; 100–300% mixed-DPI monitors;
multi-window detach and platform-native interactions; recovery after forced
termination; large and externally produced PSD/PSB/RAW corpora; print/profile
behavior; installer associations/uninstall; and each non-host build/package.
Automated screenshots are useful evidence of launch/layout, not proof of all
these interactions.

Missing or simplified requirements are tracked in this document as known gaps.
Normal regression tests assert implemented behavior and remain suitable for CI;
there are no intentionally failing tests used as a proxy for unfinished features.
Passing the existing suites does **not** mark the entire master acceptance
checklist complete.
