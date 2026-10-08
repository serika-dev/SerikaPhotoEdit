# Third-party runtime libraries

Application code and the original Serika SVG icons are MIT licensed. The prepared Windows release dynamically links the following libraries; users may replace the compatible shared libraries in its bin directory.

| Library | Prepared version | License/source |
| --- | --- | --- |
| Qt, Qt SVG, Qt Image Formats, Qt PDF | 6.8.3 | LGPL-3.0 and associated third-party terms; [exact-version Qt sources](https://download.qt.io/archive/qt/6.8/6.8.3/single/) and [Qt licensing](https://doc.qt.io/qt-6/licensing.html) |
| LibRaw | 0.22.2 | LGPL-2.1/CDDL-1.0; [LibRaw sources](https://www.libraw.org/download) |
| zlib | 1.3.1 | zlib license; [source](https://zlib.net/fossils/zlib-1.3.1.tar.gz) |
| MinGW GCC runtime | GCC 13.1 | GPL-3.0 with GCC Runtime Library Exception; [GCC sources](https://gcc.gnu.org/releases.html) |
| Image codecs | Supplied by Qt's image plugins | libjpeg-turbo, libpng, libtiff, libwebp and their bundled dependencies retain their own notices |

License texts are included in `resources/licenses` and installed under `share/serika-photoedit/licenses`. Qt PDF incorporates PDFium/Chromium dependencies; their full source attribution is in the matching Qt WebEngine source distribution. The Qt archive above provides the precise version's source and embedded attribution files. No proprietary external codec plug-ins are bundled.

The PSD/PSB implementation is original code based on the public [Adobe File Formats Specification](https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/). No Adobe artwork, editor source or proprietary color profiles are included. Built-in RGB images use Qt's sRGB profile; embedded external ICC profile bytes are retained in native documents.
