# Engine interfaces

The public interfaces are `src/document/Document.h`, `src/io/FormatIO.h` and
`src/ui/canvas/CanvasView.h`, in namespace `serika`. The implementation requires
C++20 and Qt 6.8 or newer.

Layers are stored bottom to top. Pixels use sparse 256-pixel tiles with implicit
`QImage` copy-on-write. Layer offsets are relative to their parent; use
`Document::effectiveLayerOffset` for document coordinates. Raster and vector masks
use layer-local coordinates, with independent offsets when unlinked. Selection
coverage uses document coordinates and the native 8/16/float precision; an empty
selection means no constraint. Use `makeMask`, `maskSample` and `setMaskSample`
instead of assuming a byte-per-pixel mask.

Document transactions create one undo entry per gesture: `beginTransaction`,
`touch` during the gesture, then `endTransaction`, or `cancelTransaction` to
restore the prior state. `markSaved` tracks revision identity. Smart Objects
retain source pixels; `layerImage` evaluates their filter stack and content
transform. Clear that graph when explicitly rasterizing its evaluated pixels.
