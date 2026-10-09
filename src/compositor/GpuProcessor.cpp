#include "GpuProcessor.h"
#include <QColorSpace>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#ifdef SERIKA_HAVE_OPENGL
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVersionFunctionsFactory>
#include <QSurfaceFormat>
#include <QVector4D>
#endif

namespace serika {
namespace {
bool guiThread() {
    return qobject_cast<QGuiApplication *>(QCoreApplication::instance()) &&
           QThread::currentThread() == QCoreApplication::instance()->thread();
}
double parameter(const QJsonObject &parameters, const QString &name, double defaultValue) {
    return parameters.value(name).toDouble(defaultValue);
}
#ifdef SERIKA_HAVE_OPENGL
bool floatingImage(const QImage &source) {
    return source.format() == QImage::Format_RGBA32FPx4 ||
           source.format() == QImage::Format_RGBA32FPx4_Premultiplied ||
           source.format() == QImage::Format_RGBX32FPx4;
}
QImage::Format outputFilterFormat(const QImage &source) {
    return floatingImage(source)                                                  ? QImage::Format_RGBA32FPx4
           : source.depth() > 32 || source.format() == QImage::Format_Grayscale16 ? QImage::Format_RGBA64
                                                                                  : QImage::Format_RGBA8888;
}
QImage::Format adjustmentFormat(const QImage &source) {
    if (source.depth() <= 32)
        return QImage::Format_RGBA8888;
    return source.format() == QImage::Format_RGBA32FPx4 ||
                   source.format() == QImage::Format_RGBA32FPx4_Premultiplied
               ? QImage::Format_RGBA32FPx4
               : QImage::Format_RGBA64;
}
QImage straightFloatImage(const QImage &source) {
    if (source.format() == QImage::Format_RGBA32FPx4)
        return source;
    QImage result(source.size(), QImage::Format_RGBA32FPx4);
    if (result.isNull())
        return {};
    for (int y = 0; y < source.height(); ++y) {
        auto *target = reinterpret_cast<float *>(result.scanLine(y));
        if (source.format() == QImage::Format_RGBA64) {
            const auto *pixels = reinterpret_cast<const QRgba64 *>(source.constScanLine(y));
            for (int x = 0; x < source.width(); ++x) {
                const auto pixel = pixels[x];
                target[4 * x] = pixel.red() / 65535.f;
                target[4 * x + 1] = pixel.green() / 65535.f;
                target[4 * x + 2] = pixel.blue() / 65535.f;
                target[4 * x + 3] = pixel.alpha() / 65535.f;
            }
        } else {
            const auto *pixels = source.constScanLine(y);
            for (int x = 0; x < source.width() * 4; ++x)
                target[x] = pixels[x] / 255.f;
        }
    }
    return result;
}
QImage quantizeStraightImage(const QImage &source, QImage::Format format) {
    if (format == QImage::Format_RGBA32FPx4)
        return source;
    QImage result(source.size(), format);
    if (result.isNull())
        return {};
    for (int y = 0; y < source.height(); ++y) {
        const auto *pixels = reinterpret_cast<const float *>(source.constScanLine(y));
        if (format == QImage::Format_RGBA64) {
            auto *target = reinterpret_cast<QRgba64 *>(result.scanLine(y));
            for (int x = 0; x < source.width(); ++x)
                target[x] =
                    QRgba64::fromRgba64(qRound(std::clamp(double(pixels[4 * x]), 0., 1.) * 65535),
                                        qRound(std::clamp(double(pixels[4 * x + 1]), 0., 1.) * 65535),
                                        qRound(std::clamp(double(pixels[4 * x + 2]), 0., 1.) * 65535),
                                        qRound(std::clamp(double(pixels[4 * x + 3]), 0., 1.) * 65535));
        } else {
            auto *target = result.scanLine(y);
            for (int x = 0; x < source.width() * 4; ++x)
                target[x] = uchar(qRound(std::clamp(double(pixels[x]), 0., 1.) * 255));
        }
    }
    return result;
}
constexpr auto vertexShader = R"GLSL(#version 330 core
void main() {
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";
constexpr auto adjustmentShader = R"GLSL(#version 330 core
uniform sampler2D sourceImage;
uniform int operation;
uniform bool retainHdr;
uniform bool colorize;
uniform vec4 values;
uniform vec4 extraValues;
layout(location=0) out vec4 result;
float linearize(float v) { return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
float encode(float v) { return v <= 0.0031308 ? 12.92 * v : 1.055 * pow(v, 1.0 / 2.4) - 0.055; }
vec3 toHsv(vec3 c) {
    float hi = max(c.r, max(c.g, c.b)), lo = min(c.r, min(c.g, c.b)), d = hi - lo, h = 0.0;
    if (d > 0.0) {
        if (hi == c.r) h = (c.g - c.b) / d;
        else if (hi == c.g) h = 2.0 + (c.b - c.r) / d;
        else h = 4.0 + (c.r - c.g) / d;
        h /= 6.0;
        if (h < 0.0) h += 1.0;
    }
    return vec3(h, hi <= 0.0 ? 0.0 : d / hi, hi);
}
vec3 fromHsv(vec3 c) {
    float h = fract(c.x), v = c.z, s = clamp(c.y, 0.0, 1.0);
    float f = fract(h * 6.0), p = v * (1.0 - s), q = v * (1.0 - s * f), t = v * (1.0 - s * (1.0-f));
    int sector = int(h * 6.0) % 6;
    if (sector == 0) return vec3(v,t,p);
    if (sector == 1) return vec3(q,v,p);
    if (sector == 2) return vec3(p,v,t);
    if (sector == 3) return vec3(p,q,v);
    if (sector == 4) return vec3(t,p,v);
    return vec3(v,p,q);
}
void main() {
    vec4 pixel = texelFetch(sourceImage, ivec2(gl_FragCoord.xy), 0);
    vec3 c = pixel.rgb;
    if (operation == 0) c = vec3(1.0) - c;
    else if (operation == 1) c = (c - 0.5) * values.y + 0.5 + values.x;
    else if (operation == 2) {
        for (int i=0; i<3; ++i) c[i] = encode(pow(max(0.0, linearize(c[i]) * values.x + values.y), values.z));
    } else if (operation == 3) {
        vec3 h = toHsv(c);
        h.x += values.x;
        h.y = clamp(h.y * (1.0 + values.y) + values.z * (1.0 - h.y) * h.y, 0.0, 1.0);
        if (colorize) { h.x = values.x; h.y = extraValues.x; }
        c = fromHsv(h);
        c = values.w >= 0.0 ? c + (vec3(1.0) - c) * values.w : c * (1.0 + values.w);
    } else if (operation == 4) c = vec3((max(c.r,max(c.g,c.b)) + min(c.r,min(c.g,c.b))) * 0.5);
    if (!retainHdr) c = clamp(c, 0.0, 1.0);
    result = vec4(c, clamp(pixel.a, 0.0, 1.0));
}
)GLSL";
constexpr auto blurShader = R"GLSL(#version 330 core
uniform sampler2D sourceImage;
uniform ivec2 imageSize;
uniform ivec2 direction;
uniform int radius;
uniform float kernel[73];
uniform bool firstPass;
layout(location=0) out vec4 result;
void main() {
    ivec2 position = ivec2(gl_FragCoord.xy);
    vec4 sum = vec4(0.0);
    for (int i=-36; i<=36; ++i) {
        if (i < -radius || i > radius) continue;
        ivec2 samplePosition = clamp(position + direction * i, ivec2(0), imageSize - ivec2(1));
        vec4 sampleValue = texelFetch(sourceImage, samplePosition, 0);
        if (firstPass) sampleValue.rgb *= sampleValue.a;
        sum += sampleValue * kernel[i + radius];
    }
    if (!firstPass) sum.rgb = sum.a > 0.000001 ? sum.rgb / sum.a : vec3(0.0);
    result = sum;
}
)GLSL";
struct RestoreContext {
    QOpenGLContext *previous = QOpenGLContext::currentContext();
    QSurface *surface = previous ? previous->surface() : nullptr;
    QOpenGLContext *processing = nullptr;
    ~RestoreContext() {
        if (processing)
            processing->doneCurrent();
        if (previous && previous != processing && surface)
            previous->makeCurrent(surface);
    }
};
QString glString(QOpenGLFunctions_3_3_Core *gl, GLenum name) {
    const auto value = gl->glGetString(name);
    return value ? QString::fromLatin1(reinterpret_cast<const char *>(value)) : QString();
}
bool isSoftwareRenderer(const QString &renderer) {
    const QString lower = renderer.toLower();
    for (const auto &marker : {"llvmpipe", "softpipe", "swrast", "swiftshader", "software rasterizer",
                               "gdi generic", "basic render driver", "mesa offscreen", "warp"})
        if (lower.contains(QLatin1String(marker)))
            return true;
    return false;
}
bool buildProgram(QOpenGLShaderProgram &program, const char *fragment, QString *error) {
    if (!program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader) ||
        !program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) || !program.link()) {
        *error = QStringLiteral("OpenGL shader compilation failed: %1").arg(program.log());
        return false;
    }
    return true;
}
#endif
} // namespace

struct GpuProcessor::State {
#ifdef SERIKA_HAVE_OPENGL
    QOffscreenSurface surface;
    QOpenGLContext context;
    QOpenGLFunctions_3_3_Core *gl = nullptr;
    QOpenGLShaderProgram adjustment;
    QOpenGLShaderProgram blur;
    GLuint vertexArray = 0;
    ~State() {
        RestoreContext restore;
        restore.processing = &context;
        if (surface.isValid() && context.makeCurrent(&surface)) {
            if (vertexArray && gl)
                gl->glDeleteVertexArrays(1, &vertexArray);
            adjustment.removeAllShaders();
            blur.removeAllShaders();
        }
    }
#endif
};

QString GpuDiagnostics::summary() const {
    QString text = available ? QStringLiteral("OpenGL shader processing available")
                             : QStringLiteral("CPU processing: %1").arg(reason);
    if (!renderer.isEmpty())
        text += QStringLiteral("\nRenderer: %1\nVendor: %2\nOpenGL: %3").arg(renderer, vendor, version);
    if (softwareRenderer)
        text += QStringLiteral("\nSoftware OpenGL renderer; hardware acceleration is unavailable.");
    text += QStringLiteral("\nGPU processing is %1. Completed shader operations: %2.")
                .arg(enabled ? "enabled" : "disabled")
                .arg(completedOperations);
    if (!lastOperation.isEmpty())
        text += QStringLiteral("\nLast operation: %1 (%2)").arg(lastOperation, lastBackend);
    if (!lastFallback.isEmpty())
        text += QStringLiteral("\nCPU fallback: %1").arg(lastFallback);
    return text;
}
GpuProcessor &GpuProcessor::instance() {
    static GpuProcessor processor;
    return processor;
}
GpuProcessor::GpuProcessor() {
#ifdef SERIKA_HAVE_OPENGL
    m_diagnostics.compiled = true;
#else
    m_diagnostics.reason = "This build has no Qt OpenGL processing support.";
#endif
}
GpuProcessor::~GpuProcessor() = default;
void GpuProcessor::setEnabled(bool enabled) { m_enabled.store(enabled); }
bool GpuProcessor::enabled() const { return m_enabled.load(); }
QStringList GpuProcessor::supportedAdjustments() {
    return {"Invert", "Brightness/Contrast", "Exposure", "Hue/Saturation", "Vibrance", "Desaturate"};
}
QStringList GpuProcessor::supportedFilters() { return {"Gaussian Blur", "Gaussian", "Field Blur"}; }
void GpuProcessor::shutdown() {
    if (guiThread()) {
        m_state.reset();
        m_diagnostics.available = false;
        m_diagnostics.reason = "OpenGL processing context has been released.";
    }
}
bool GpuProcessor::initialize() {
#ifndef SERIKA_HAVE_OPENGL
    return false;
#else
    if (!guiThread())
        return false;
    if (m_state)
        return m_diagnostics.available;
    m_diagnostics.reason.clear();
    auto state = std::make_unique<State>();
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);
    state->context.setFormat(format);
    if (!state->context.create() || !state->context.isValid()) {
        m_diagnostics.reason = "The platform could not create an OpenGL 3.3 context.";
        m_state = std::move(state);
        return false;
    }
    state->surface.setFormat(state->context.format());
    state->surface.create();
    RestoreContext restore;
    restore.processing = &state->context;
    if (!state->surface.isValid() || !state->context.makeCurrent(&state->surface)) {
        m_diagnostics.reason = "The platform could not make an offscreen OpenGL context current.";
        m_state = std::move(state);
        return false;
    }
    state->gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(&state->context);
    if (!state->gl || !state->gl->initializeOpenGLFunctions()) {
        m_diagnostics.reason = "Desktop OpenGL 3.3 functions are unavailable.";
        m_state = std::move(state);
        return false;
    }
    auto *gl = state->gl;
    m_diagnostics.vendor = glString(gl, GL_VENDOR);
    m_diagnostics.renderer = glString(gl, GL_RENDERER);
    m_diagnostics.version = glString(gl, GL_VERSION);
    gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_diagnostics.maximumTextureSize);
    m_diagnostics.softwareRenderer = isSoftwareRenderer(m_diagnostics.renderer);
    if (m_diagnostics.softwareRenderer) {
        m_diagnostics.reason = "Only a software OpenGL renderer is available.";
    } else if (m_diagnostics.renderer.isEmpty()) {
        m_diagnostics.reason = "The OpenGL driver did not identify its renderer.";
    } else if (buildProgram(state->adjustment, adjustmentShader, &m_diagnostics.reason) &&
               buildProgram(state->blur, blurShader, &m_diagnostics.reason)) {
        QOpenGLFramebufferObjectFormat framebufferFormat;
        framebufferFormat.setInternalTextureFormat(GL_RGBA32F);
        QOpenGLFramebufferObject probe(QSize(1, 1), framebufferFormat);
        if (!probe.isValid())
            m_diagnostics.reason = "The OpenGL driver does not support RGBA32F processing framebuffers.";
        else {
            gl->glGenVertexArrays(1, &state->vertexArray);
            m_diagnostics.available = state->vertexArray != 0 && gl->glGetError() == GL_NO_ERROR;
            if (!m_diagnostics.available)
                m_diagnostics.reason = "The OpenGL driver could not initialize shader processing resources.";
        }
    }
    m_state = std::move(state);
    return m_diagnostics.available;
#endif
}
GpuDiagnostics GpuProcessor::diagnostics(bool probe) {
    if (!guiThread()) {
        GpuDiagnostics result;
        result.enabled = enabled();
#ifdef SERIKA_HAVE_OPENGL
        result.compiled = true;
#endif
        result.reason = "OpenGL diagnostics must be requested on the GUI thread.";
        return result;
    }
    if (probe)
        initialize();
    auto result = m_diagnostics;
    result.enabled = enabled();
    return result;
}
GpuProcessingResult GpuProcessor::fallback(const QString &operation, const QString &reason) {
    if (guiThread()) {
        m_diagnostics.lastOperation = operation;
        m_diagnostics.lastBackend = "CPU";
        m_diagnostics.lastFallback = reason;
    }
    return {{}, false, reason};
}
GpuProcessingResult GpuProcessor::processAdjustment(const QImage &source, const QString &name,
                                                    const QJsonObject &parameters) {
    if (!enabled())
        return fallback(name, "GPU processing is disabled.");
    if (!supportedAdjustments().contains(name))
        return fallback(name, "This adjustment is implemented by the CPU backend.");
    return process(source, name, parameters, false);
}
GpuProcessingResult GpuProcessor::processFilter(const QImage &source, const QString &name,
                                                const QJsonObject &parameters) {
    if (!enabled())
        return fallback(name, "GPU processing is disabled.");
    const QString key = name.trimmed().toLower();
    if (key != "gaussian blur" && key != "gaussian" && key != "field blur")
        return fallback(name, "This filter is implemented by the CPU backend.");
    const double radius = parameter(parameters, "radius", 3);
    if (radius < .01)
        return fallback(name, "A zero-radius Gaussian is a CPU no-op.");
    if (radius > 12)
        return fallback(name, "Gaussian radii above 12 use the CPU box approximation.");
    return process(source, name, parameters, true);
}
GpuProcessingResult GpuProcessor::process(const QImage &source, const QString &name,
                                          const QJsonObject &parameters, bool blur) {
    if (source.isNull())
        return fallback(name, "The source image is empty.");
    if (!guiThread())
        return fallback(name, "OpenGL processing is available only on the GUI thread.");
    for (auto it = parameters.begin(); it != parameters.end(); ++it)
        if (it->isDouble() &&
            (!std::isfinite(it->toDouble()) || std::abs(it->toDouble()) > std::numeric_limits<float>::max()))
            return fallback(name, "An operation parameter exceeds shader numeric limits.");
    if (!initialize())
        return fallback(name, m_diagnostics.reason);
#ifndef SERIKA_HAVE_OPENGL
    Q_UNUSED(blur)
    return fallback(name, m_diagnostics.reason);
#else
    const int maximum = m_diagnostics.maximumTextureSize;
    if (source.width() > maximum || source.height() > maximum)
        return fallback(
            name, QStringLiteral("Image dimensions exceed the OpenGL texture limit (%1).").arg(maximum));
    // One input and two RGBA32F targets; bound staging/VRAM allocations and defer huge images to CPU
    // dispatch.
    if (quint64(source.width()) * source.height() > 32ULL * 1024 * 1024)
        return fallback(name, "Images above 32 megapixels use CPU processing to bound GPU allocations.");
    RestoreContext restore;
    restore.processing = &m_state->context;
    if (!m_state->context.makeCurrent(&m_state->surface))
        return fallback(name, "The OpenGL processing context could not be made current.");
    auto *gl = m_state->gl;
    for (int attempt = 0; attempt < 16; ++attempt) {
        if (gl->glGetError() == GL_NO_ERROR)
            break;
        if (attempt == 15)
            return fallback(name, "The OpenGL context reports persistent driver errors.");
    }
    // Match each CPU dispatcher's normalization before uploading; float/HDR adjustment semantics differ from
    // filters.
    const QImage normalized = blur ? source : source.convertToFormat(adjustmentFormat(source));
    // Qt's integer-to-float conversion loses straight RGB precision at very low alpha.
    // Adjustment CPU code reads straight integer channels; filters instead use Qt float conversion.
    const QImage input =
        blur ? normalized.convertToFormat(QImage::Format_RGBA32FPx4) : straightFloatImage(normalized);
    if (input.isNull())
        return fallback(name, "The image upload buffer could not be allocated.");
    QOpenGLFramebufferObjectFormat framebufferFormat;
    framebufferFormat.setInternalTextureFormat(GL_RGBA32F);
    QOpenGLFramebufferObject first(source.size(), framebufferFormat);
    QOpenGLFramebufferObject second(source.size(), framebufferFormat);
    if (!first.isValid() || !second.isValid())
        return fallback(name, "RGBA32F processing targets could not be allocated.");
    GLuint texture = 0;
    gl->glGenTextures(1, &texture);
    struct TextureGuard {
        QOpenGLFunctions_3_3_Core *gl;
        GLuint texture;
        ~TextureGuard() { gl->glDeleteTextures(1, &texture); }
    } textureGuard{gl, texture};
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, texture);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, input.width(), input.height(), 0, GL_RGBA, GL_FLOAT,
                     input.constBits());
    if (gl->glGetError() != GL_NO_ERROR)
        return fallback(name, "The driver rejected the float image texture upload.");
    gl->glViewport(0, 0, input.width(), input.height());
    gl->glDisable(GL_BLEND);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glDisable(GL_SCISSOR_TEST);
    gl->glDisable(GL_FRAMEBUFFER_SRGB);
    gl->glBindVertexArray(m_state->vertexArray);
    auto &program = blur ? m_state->blur : m_state->adjustment;
    if (!program.bind())
        return fallback(name, "The shader program could not be bound.");
    program.setUniformValue("sourceImage", 0);
    first.bind();
    if (blur) {
        double sigma = std::clamp(parameter(parameters, "radius", 3), 0., 12.);
        int radius = 0;
        QVector<float> kernel(1, 1);
        if (sigma >= .01) {
            sigma = std::max(.1, sigma);
            radius = int(std::ceil(3 * sigma));
            kernel.resize(2 * radius + 1);
            double sum = 0;
            for (int i = -radius; i <= radius; ++i) {
                kernel[i + radius] = float(std::exp(-i * i / (2 * sigma * sigma)));
                sum += kernel[i + radius];
            }
            for (auto &weight : kernel)
                weight /= sum;
        }
        const GLint sizeLocation = program.uniformLocation("imageSize");
        const GLint directionLocation = program.uniformLocation("direction");
        gl->glUniform2i(sizeLocation, input.width(), input.height());
        gl->glUniform2i(directionLocation, 1, 0);
        program.setUniformValue("radius", radius);
        program.setUniformValueArray("kernel", kernel.constData(), int(kernel.size()), 1);
        program.setUniformValue("firstPass", true);
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        second.bind();
        gl->glBindTexture(GL_TEXTURE_2D, first.texture());
        gl->glUniform2i(directionLocation, 0, 1);
        program.setUniformValue("firstPass", false);
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    } else {
        int operation = 0;
        QVector4D values, extra;
        if (name == "Brightness/Contrast") {
            operation = 1;
            const double contrast = std::clamp(parameter(parameters, "contrast", 0) / 100., -.99, .99);
            values.setX(
                float(parameter(parameters, "brightness", parameter(parameters, "amount", 0)) / 100.));
            values.setY(float(contrast >= 0 ? 1 / (1 - contrast) : 1 + contrast));
        } else if (name == "Exposure") {
            operation = 2;
            const double gain =
                std::pow(2, parameter(parameters, "exposure", parameter(parameters, "amount", 0) / 50.));
            if (!std::isfinite(gain) || gain > std::numeric_limits<float>::max())
                return fallback(name, "Exposure gain exceeds shader numeric limits.");
            values.setX(float(gain));
            values.setY(float(parameter(parameters, "offset", 0)));
            values.setZ(float(1 / std::max(.01, parameter(parameters, "gamma", 1))));
        } else if (name == "Hue/Saturation" || name == "Vibrance") {
            operation = 3;
            values.setX(float(parameter(parameters, "hue", 0) / 360.));
            values.setY(float(parameter(parameters, "saturation",
                                        name == "Vibrance" ? 0 : parameter(parameters, "amount", 0)) /
                              100.));
            values.setZ(float(parameter(parameters, "vibrance",
                                        name == "Vibrance" ? parameter(parameters, "amount", 0) : 0) /
                              100.));
            values.setW(float(parameter(parameters, "lightness", 0) / 100.));
            extra.setX(float(std::clamp(parameter(parameters, "saturation", 25) / 100., 0., 1.)));
        } else if (name == "Desaturate")
            operation = 4;
        program.setUniformValue("operation", operation);
        program.setUniformValue("values", values);
        program.setUniformValue("extraValues", extra);
        program.setUniformValue("colorize", parameters.value("colorize").toBool());
        program.setUniformValue("retainHdr",
                                normalized.format() == QImage::Format_RGBA32FPx4 && name == "Exposure");
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    QImage output(source.size(), QImage::Format_RGBA32FPx4);
    if (output.isNull())
        return fallback(name, "The image readback buffer could not be allocated.");
    gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl->glReadPixels(0, 0, output.width(), output.height(), GL_RGBA, GL_FLOAT, output.bits());
    const GLenum error = gl->glGetError();
    program.release();
    gl->glBindVertexArray(0);
    if (error != GL_NO_ERROR)
        return fallback(name, QStringLiteral("OpenGL shader/readback failed (error %1).").arg(error));
    for (int y = 0; y < output.height(); ++y) {
        const auto *pixels = reinterpret_cast<const float *>(output.constScanLine(y));
        for (int x = 0; x < output.width() * 4; ++x)
            if (!std::isfinite(pixels[x]))
                return fallback(name, "The shader produced a non-finite channel value.");
    }
    output = blur ? output.convertToFormat(outputFilterFormat(source))
                  : quantizeStraightImage(output, adjustmentFormat(source));
    if (output.isNull())
        return fallback(name, "The native-depth result could not be allocated.");
    output.setColorSpace(source.colorSpace());
    output.setDotsPerMeterX(source.dotsPerMeterX());
    output.setDotsPerMeterY(source.dotsPerMeterY());
    output.setDevicePixelRatio(source.devicePixelRatio());
    ++m_diagnostics.completedOperations;
    m_diagnostics.lastOperation = name;
    m_diagnostics.lastBackend = "OpenGL shader";
    m_diagnostics.lastFallback.clear();
    return {output, true, {}};
#endif
}
} // namespace serika
