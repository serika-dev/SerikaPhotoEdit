# Format implementation

SPE/SPEB use the same versioned chunk container. All document-model fields are saved,
including sparse 256-pixel tiles, their native 8/16/float format, editable text, paths,
groups, masks, effects, selections, guides, ICC data and JSON metadata. Every chunk has
a CRC-32. A checked copy of the JSON header and an end marker detect incomplete writes.
`QSaveFile` performs replacement atomically; the native writer flushes the file handle
before committing. Tile samples use little endian; PSD samples use big endian.

PSD/PSB code is independently implemented from the public [Adobe format
specification](https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/).
RGB, grayscale, duotone, CMYK and Lab input at 8/16/32 bits is supported. The reader
decodes raw, PackBits and, when zlib is linked, ZIP and ZIP prediction. CMYK/Lab are
converted to editable RGB; original profile bytes are retained in metadata. Layers,
groups, positions, masks, opacity, fill and all listed blend keys are mapped to the model.
Invert, legacy Brightness/Contrast, Posterize and Threshold adjustment records are read.
Every unimplemented resource/tag is listed in the import report. Unknown per-layer
blocks and image resources are retained as base64 source data in metadata.

The PSD writer emits an ordinary RGB composite and raw channel planes with layer,
group and mask records. Standard Unicode names, IDs, blend keys, ICC, resolution and
guides are emitted. Editable Serika text/shapes/adjustments/styles/smart-object fields
are additionally stored in private `sPEd` blocks with raster compatibility pixels.
Those fields are editable when reopened in Serika; other editors see their raster
compatibility form. Native Adobe type-engine, smart-object and effect descriptors are
not synthesized. `sPEi` preserves Serika's model ordering when reopening groups.

The PSD reader has a 2 GB whole-file working-memory limit and a 512 MB per-plane
limit. SPE is streamed chunk by chunk and is not subject to the PSD whole-file limit.
Large document support remains constrained by available RAM and Qt image allocations.

RAW uses dynamically linked LibRaw and produces genuine 16-bit RGB before Develop.
The metadata flag `rawPendingDevelop` tells the UI to show Develop. Camera metadata,
source path and sidecar settings are retained. The bundled Windows build enables DNG
deflate; optional lossy-DNG/Foveon codecs depend on LibRaw's build configuration.

PNG/JPEG/TIFF/WebP/BMP/GIF/SVG and other raster formats use installed Qt image plugins.
Only formats reported by those plugins are advertised. TIFF pages and GIF frames are
imported as layers; GIF timing is retained in metadata. JPEG XL, AVIF, HEIF, EXR and
JPEG 2000 require additional compatible codec plugins and are not bundled by default.
TGA (raw/RLE, true color/grayscale/indexed) and Radiance HDR (float RGBE, raw/legacy/RLE)
are implemented directly. SVG imports as pixels. Qt PDF renders each PDF page to a
pixel layer at a specified DPI, default 150, when the dynamically linked Pdf module is
enabled.

Filters use CPU float working buffers and preserve 8/16/float output precision.
Gaussian blur uses normalized separable kernels, with three box passes for large
radii. Noise is deterministic by seed. Median, bilateral/disk blur, unsharp masking,
convolution, morphological filters, geometric resampling, procedural noise and
Voronoi filters are implemented directly. These are simplified algorithms rather
than exact reproductions of another editor's proprietary filters.
