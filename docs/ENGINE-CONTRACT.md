# Shared implementation contract

The headers in src/document/Document.h, src/io/FormatIO.h and src/ui/canvas/CanvasView.h are the shared interfaces. Namespace serika. Qt 6.8+, C++20. Layers are stored bottom to top. Pixels use sparse 256px tiles, with implicit QImage copy-on-write. Offset is in document coordinates. Selection is a document-sized grayscale mask; an empty mask means no constraint. Document transactions are one undo step per gesture; beginTransaction, touch during gesture, endTransaction. markSaved is revision-aware.

Engine agent owns document + compositor + tests/core_tests.cpp. IO agent owns io + filters + tests/io_tests.cpp. Canvas agent owns ui/canvas + tools + original tool icons. Root owns CMake, app, other UI, packaging and build integration. Coordinate interface changes before making them.
