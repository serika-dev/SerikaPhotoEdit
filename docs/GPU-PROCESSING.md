# GPU processing

Serika PhotoEdit has an optional desktop OpenGL 3.3 shader processor. CPU processing is the default. Enable it under **Preferences > Performance**. **Help > GPU Diagnostics** identifies the OpenGL vendor, renderer, version, texture limit, fallback reason, and completed shader operations. `SerikaPhotoEdit --gpu-info` prints the same capabilities; `--gpu` enables supported operations for that invocation, with CPU fallback.

The backend executes fragment shaders into RGBA32F framebuffer textures and reads their results back into the document's native raster format. These are actual image operations, used by the adjustment/filter dispatchers for destructive edits, previews, adjustment layers, and smart filters. The regular layer compositor, brush strokes, selection algorithms, and unsupported filters continue to execute on the CPU.

Supported shader operations are:

| Operation | Behavior |
| --- | --- |
| Invert | RGB inversion with alpha preserved |
| Brightness/Contrast | Existing brightness and contrast formulas, including negative contrast |
| Exposure | Existing sRGB decode/exposure/offset/gamma/encode formula; 32-bit float results retain highlights above 1 |
| Hue/Saturation | Hue, saturation, lightness, and colorize |
| Vibrance | Existing saturation and vibrance formula |
| Desaturate | Existing maximum/minimum RGB midpoint formula |
| Gaussian Blur, Gaussian, Field Blur | Two separable convolution passes with premultiplied alpha and clamped image edges; sigma from 0.01 through 12 |

The processor matches the CPU dispatchers' input normalization and output format. Native RGBA 8-bit, RGBA 16-bit, and RGBA 32-bit float documents use float texture upload and float readback, without an intermediate 8-bit image. Color-space metadata, resolution, and device pixel ratio are retained. The shaders evaluate the same encoded RGB formulas as their CPU counterparts; proof-display color conversion is a separate operation.

GPU and CPU results can differ slightly because shaders use single-precision arithmetic. Regression tests allow at most one 8-bit channel step, approximately three 16-bit steps, or 0.00006 in standard float fixtures. A separate HDR fixture allows 0.0002. Native 16-bit tests also verify that differences smaller than an 8-bit step survive shader processing.

## Fallback and limits

The dispatcher uses a GPU result only after shader execution and readback succeed. A failed request leaves the existing CPU implementation responsible for the operation; diagnostics label that result CPU and record the reason. An OpenGL context being available alone does not increment the shader operation counter.

- OpenGL processing requires the GUI thread and a desktop OpenGL 3.3 core context with RGBA32F framebuffer support. Worker-thread requests use CPU processing.
- Recognized software renderers, including llvmpipe, SwiftShader, GDI Generic, and WARP, are identified in diagnostics and excluded from hardware acceleration.
- Unsupported adjustments and filters use CPU processing. Gaussian sigma above 12 retains the existing CPU box-filter approximation; zero radius retains its exact CPU no-op.
- Images beyond the driver's maximum texture dimensions or above 32 megapixels use CPU processing. This backend processes whole images and does not implement tiled GPU streaming.
- Resource allocation, shader compilation, context activation, texture upload, or readback failure triggers CPU fallback.
- There is no promise that small images run faster: image conversion, upload, synchronization, and readback have overhead. This implementation does not accelerate the entire application or replace the CPU compositor.

## Building and verification

Link optional `Qt6::OpenGL` into `serika_engine` and define `SERIKA_HAVE_OPENGL=1` when that component is available. Without it, the same public API builds and reports that GPU support was omitted. Qt's context/offscreen-surface support lives in Qt Gui; its framebuffer and shader-program helpers require Qt OpenGL. See [Qt OpenGL context](https://doc.qt.io/qt-6.8/qopenglcontext.html), [offscreen surface](https://doc.qt.io/qt-6.8/qoffscreensurface.html), [framebuffer object](https://doc.qt.io/qt-6.8/qopenglframebufferobject.html), and [shader program](https://doc.qt.io/qt-6.8/qopenglshaderprogram.html).

Run `gpu_tests -platform windows` on Windows to exercise a native hardware context. It requires no visible widget. `SERIKA_REQUIRE_GPU=1` makes missing hardware a test failure; without that variable, shader parity cases explicitly skip when unavailable and CPU fallback checks still run. An offscreen Qt platform can lack native OpenGL support, so an offscreen-only passing suite does not establish GPU execution.

The suite compares CPU and shader pixels across all three native depths, alpha-sensitive Gaussian kernels, very transparent straight-color pixels, 16-bit detail, HDR exposure, driver limits, and document undo. It also verifies disabled/unsupported/worker-thread fallbacks and reports the driver used. Integer adjustment upload/readback copies straight channels directly; this avoids low-alpha precision loss in Qt's intermediate premultiplication conversion.

On 2026-10-09, the native Windows 11 run with Qt 6.8.3 reported **AMD Radeon(TM) Graphics**, vendor **ATI Technologies Inc.**, and **OpenGL 3.3.0 Core Profile Context 23.19.12.03.240603**. Its result was **44 passed, 0 failed, 1 skipped**. All adjustment, blur, native-depth, HDR, very-transparent-pixel, and document-operation shader checks executed successfully. The expected skip was the unavailable-platform case because this machine had hardware OpenGL. This verifies real shader execution on that device; it does not establish a performance improvement or support on another driver. Linux and macOS runtime execution remains unverified.

Call `GpuProcessor::shutdown()` on the GUI thread before application teardown. Contexts and shaders are cached between operations and can initialize again after shutdown.
