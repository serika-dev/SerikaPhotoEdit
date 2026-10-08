# Third-party runtime libraries

Application code and the original Serika SVG icons are MIT licensed. The prepared Windows release dynamically links the following libraries; users may replace the compatible shared libraries in its bin directory.

| Library | Prepared version | License/source |
| --- | --- | --- |
| Qt, Qt SVG, Qt Image Formats, Qt PDF | 6.8.3 | LGPL-3.0 and associated third-party terms; [exact-version Qt sources](https://download.qt.io/archive/qt/6.8/6.8.3/single/) and [Qt licensing](https://doc.qt.io/qt-6/licensing.html) |
| LibRaw | 0.22.2 | LGPL-2.1/CDDL-1.0; [LibRaw sources](https://www.libraw.org/download) |
| zlib | 1.3.1 | zlib license; [source](https://zlib.net/fossils/zlib-1.3.1.tar.gz) |
| MinGW GCC runtime | GCC 13.1 | GPL-3.0 with GCC Runtime Library Exception; [GCC sources](https://gcc.gnu.org/releases.html) |
| Image codecs | Supplied by Qt's image plugins | libjpeg-turbo 3.1.0, libpng 1.6.47, libtiff 4.7.0 and libwebp 1.5.0; exact Qt 6.8.3 notices are bundled |
| PDFium and Chromium support code | Qt PDF 6.8.3 source snapshot | PDFium BSD/Apache-2.0 and Chromium BSD notices, plus their bundled dependency notices |

License texts are included in `resources/licenses` and installed under `share/serika-photoedit/licenses`. `Qt-6.8.3-THIRD-PARTY-NOTICES.txt` retains 53 upstream attribution entries and their license texts from the Qt Base, Qt SVG and Qt Image Formats v6.8.3 source tags. It includes conditionally compiled platform code, so some entries are inactive in a particular build.

`Qt-PDF-6.8.3-THIRD-PARTY-NOTICES.txt` includes the full PDFium and Chromium notices and the source snapshot's Abseil, FreeType, ICU, JPEG, PNG, zlib, Anti-Grain Geometry, Big Integer Library, Little CMS, OpenJPEG and TIFF notices. These files come from commit [`55749ed0af5869215b88007df0cba430746583ae`](https://github.com/qt/qtwebengine-chromium/tree/55749ed0af5869215b88007df0cba430746583ae), the submodule pinned by [Qt WebEngine v6.8.3](https://github.com/qt/qtwebengine/tree/v6.8.3/src/3rdparty). `Qt-6.8.3-NOTICE-SOURCES.json` records each exact upstream file URL and its SHA-256 digest. Separate `PDFium.txt` and `Chromium.txt` copies make their principal notices easy to find. Serika bundles Qt PDF; it does not bundle the Chromium browser.

Portions of this software are copyright (c) the [FreeType Project](https://freetype.org). All rights reserved. This software is based in part on the work of the Independent JPEG Group. No proprietary external codec plug-ins are bundled. The Qt source archive linked above supplies the matching library source; compatible dynamically linked libraries may be replaced without changing the application source.

The PSD/PSB implementation is original code based on the public [Adobe File Formats Specification](https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/). No Adobe artwork, editor source or proprietary color profiles are included. Built-in RGB images use Qt's sRGB profile; embedded external ICC profile bytes are retained in native documents.
