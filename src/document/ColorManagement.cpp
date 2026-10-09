#include "ColorManagement.h"
#include "Document.h"
#include <QColorSpace>
#include <QFile>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#ifdef SERIKA_HAVE_LCMS2
#include <lcms2.h>
#endif

namespace serika {
namespace {
constexpr qsizetype MaxProfileBytes = 16 * 1024 * 1024;
constexpr qint64 MaxSampleBytes = 512ll * 1024 * 1024;
bool fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}
#ifdef SERIKA_HAVE_LCMS2
bool boundedProfile(const QByteArray &bytes, QString *error) {
    if (bytes.size() < 132 || bytes.size() > MaxProfileBytes)
        return fail(error, "ICC profiles must contain 132 bytes to 16 MiB.");
    const auto declared = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData()));
    if (declared < 132 || declared > quint32(bytes.size()) || bytes.mid(36, 4) != "acsp")
        return fail(error, "The ICC header is invalid or truncated.");
    return true;
}
#endif
QByteArray decodeProfile(const QJsonValue &value) {
    const auto text = value.toString();
    if (text.size() > (MaxProfileBytes + 2) / 3 * 4)
        return QByteArray("ICC encoding exceeds profile limit");
    const auto result = QByteArray::fromBase64(text.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    return !text.isEmpty() && result.isEmpty() ? QByteArray("invalid ICC encoding") : result;
}
bool boundedImage(const QImage &image, QString *error) {
    if (image.isNull() || qint64(image.width()) * image.height() * 8 > MaxSampleBytes)
        return fail(error, "Color conversion requires a nonempty image of at most 64 million pixels.");
    return true;
}
#ifdef SERIKA_HAVE_LCMS2
struct CmsContext {
    QString lastError;
    cmsContext id = nullptr;
    CmsContext() {
        id = cmsCreateContext(nullptr, this);
        if (id)
            cmsSetLogErrorHandlerTHR(id, [](cmsContext context, cmsUInt32Number, const char *message) {
                static_cast<CmsContext *>(cmsGetContextUserData(context))->lastError =
                    QString::fromUtf8(message);
            });
    }
    ~CmsContext() {
        if (id)
            cmsDeleteContext(id);
    }
    QString error(const QString &prefix) const {
        return lastError.isEmpty() ? prefix : prefix + ": " + lastError;
    }
};
struct ProfileDeleter {
    void operator()(void *profile) const {
        if (profile)
            cmsCloseProfile(profile);
    }
};
struct TransformDeleter {
    void operator()(void *transform) const {
        if (transform)
            cmsDeleteTransform(transform);
    }
};
using Profile = std::unique_ptr<void, ProfileDeleter>;
using Transform = std::unique_ptr<void, TransformDeleter>;
Profile openProfile(CmsContext &context, const QByteArray &bytes, QString *error) {
    if (!context.id) {
        fail(error, "LittleCMS could not allocate a color-management context.");
        return {};
    }
    if (!boundedProfile(bytes, error))
        return {};
    Profile profile(cmsOpenProfileFromMemTHR(context.id, bytes.constData(), cmsUInt32Number(bytes.size())));
    if (!profile)
        fail(error, context.error("Cannot open the ICC profile"));
    return profile;
}
Profile rgbProfile(CmsContext &context, const QByteArray &bytes, QString *error) {
    if (!context.id) {
        fail(error, "LittleCMS could not allocate a color-management context.");
        return {};
    }
    Profile profile =
        bytes.isEmpty() ? Profile(cmsCreate_sRGBProfileTHR(context.id)) : openProfile(context, bytes, error);
    if (profile && (cmsGetColorSpace(profile.get()) != cmsSigRgbData ||
                    cmsGetDeviceClass(profile.get()) == cmsSigLinkClass)) {
        fail(error, "The source/display profile must describe RGB colors, not a device link.");
        return {};
    }
    return profile;
}
QString profileName(cmsHPROFILE profile) {
    const auto size = cmsGetProfileInfoASCII(profile, cmsInfoDescription, "en", "US", nullptr, 0);
    if (!size || size > 65536)
        return "Unnamed ICC profile";
    QByteArray text(int(size), '\0');
    cmsGetProfileInfoASCII(profile, cmsInfoDescription, "en", "US", text.data(), size);
    return QString::fromLatin1(text.constData()).trimmed();
}
QString signatureName(cmsUInt32Number signature) {
    char value[5] = {char(signature >> 24), char(signature >> 16), char(signature >> 8), char(signature), 0};
    return QString::fromLatin1(value).trimmed();
}
bool cmykOutput(cmsHPROFILE profile, RenderingIntent intent, bool proofing, QString *error) {
    if (int(intent) < 0 || int(intent) > 3)
        return fail(error, "Select one of the four ICC rendering intents.");
    if (cmsGetColorSpace(profile) != cmsSigCmykData || cmsGetDeviceClass(profile) != cmsSigOutputClass)
        return fail(error, "Choose a genuine CMYK output/printer ICC profile. RGB and device-link profiles "
                           "are not accepted.");
    if (!cmsIsIntentSupported(profile, cmsUInt32Number(intent), LCMS_USED_AS_OUTPUT))
        return fail(error, "This CMYK profile has no usable separation transform for the selected intent.");
    if (proofing && !cmsIsIntentSupported(profile, INTENT_RELATIVE_COLORIMETRIC, LCMS_USED_AS_INPUT))
        return fail(error, "This CMYK profile has no usable device-to-color transform for soft proofing.");
    return true;
}
QByteArray sourceProfile(const QImage &source, const QByteArray &explicitProfile) {
    if (!explicitProfile.isEmpty())
        return explicitProfile;
    return source.colorSpace().isValid() ? source.colorSpace().iccProfile() : QByteArray();
}
#endif
} // namespace

QJsonObject ProofSettings::toJson() const {
    return {{"outputProfile", QString::fromLatin1(outputProfile.toBase64())},
            {"displayProfile", QString::fromLatin1(displayProfile.toBase64())},
            {"profileName", profileName},
            {"displayName", displayName},
            {"intent", int(intent)},
            {"blackPointCompensation", blackPointCompensation},
            {"simulatePaper", simulatePaper},
            {"enabled", enabled},
            {"gamutWarning", gamutWarning}};
}
ProofSettings ProofSettings::fromJson(const QJsonObject &json) {
    ProofSettings settings;
    settings.outputProfile = decodeProfile(json.value("outputProfile"));
    settings.displayProfile = decodeProfile(json.value("displayProfile"));
    settings.profileName = json.value("profileName").toString();
    settings.displayName = json.value("displayName").toString();
    settings.intent = RenderingIntent(json.value("intent").toInt(1));
    settings.blackPointCompensation = json.value("blackPointCompensation").toBool(true);
    settings.simulatePaper = json.value("simulatePaper").toBool();
    settings.enabled = json.value("enabled").toBool();
    settings.gamutWarning = json.value("gamutWarning").toBool();
    return settings;
}
bool CmykImage::isValid() const {
    const qint64 count = qint64(size.width()) * size.height();
    return size.width() > 0 && size.height() > 0 && (bitDepth == 8 || bitDepth == 16) &&
           count <= MaxSampleBytes / (4 * (bitDepth / 8)) && samples.size() == count * 4 * (bitDepth / 8) &&
           !iccProfile.isEmpty();
}
QByteArray CmykImage::channelSamples(int channel) const {
    if (!isValid() || channel < 0 || channel > 3)
        return {};
    const auto bytes = bitDepth / 8;
    const qsizetype count = qsizetype(size.width()) * size.height();
    QByteArray result(count * bytes, Qt::Uninitialized);
    for (qsizetype i = 0; i < count; ++i)
        std::memcpy(result.data() + i * bytes, samples.constData() + (i * 4 + channel) * bytes, bytes);
    return result;
}
bool colorManagementAvailable() {
#ifdef SERIKA_HAVE_LCMS2
    return true;
#else
    return false;
#endif
}
QString colorManagementVersion() {
#ifdef SERIKA_HAVE_LCMS2
    const auto version = cmsGetEncodedCMMversion();
    return QString("LittleCMS %1.%2.%3").arg(version / 1000).arg((version % 1000) / 10).arg(version % 10);
#else
    return "LittleCMS unavailable in this build";
#endif
}
bool inspectIccProfile(const QByteArray &bytes, IccProfileInfo *info, QString *error) {
    if (error)
        error->clear();
#ifdef SERIKA_HAVE_LCMS2
    CmsContext context;
    auto profile = openProfile(context, bytes, error);
    if (!profile)
        return false;
    if (info) {
        info->name = profileName(profile.get());
        info->colorSpace = signatureName(cmsGetColorSpace(profile.get()));
        info->deviceClass = signatureName(cmsGetDeviceClass(profile.get()));
        info->cmykOutput = cmsGetColorSpace(profile.get()) == cmsSigCmykData &&
                           cmsGetDeviceClass(profile.get()) == cmsSigOutputClass;
    }
    return true;
#else
    Q_UNUSED(bytes)
    Q_UNUSED(info)
    return fail(error,
                "This build has no LittleCMS backend; ICC proofing and CMYK conversion are unavailable.");
#endif
}
QByteArray readIccProfile(const QString &path, QString *error) {
    if (error)
        error->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, "Cannot open ICC profile: " + file.errorString());
        return {};
    }
    if (file.size() < 132 || file.size() > MaxProfileBytes) {
        fail(error, "ICC profiles must contain 132 bytes to 16 MiB.");
        return {};
    }
    const auto bytes = file.readAll();
    return inspectIccProfile(bytes, nullptr, error) ? bytes : QByteArray();
}
bool validateProofSettings(const ProofSettings &settings, QString *error) {
    if (error)
        error->clear();
#ifdef SERIKA_HAVE_LCMS2
    CmsContext context;
    auto profile = openProfile(context, settings.outputProfile, error);
    if (!profile || !cmykOutput(profile.get(), settings.intent, true, error))
        return false;
    auto display = rgbProfile(context, settings.displayProfile, error);
    if (!display)
        return false;
    auto input = rgbProfile(context, {}, error);
    if (!input)
        return false;
    const auto flags = cmsFLAGS_SOFTPROOFING |
                       (settings.blackPointCompensation ? cmsFLAGS_BLACKPOINTCOMPENSATION : 0u) |
                       (settings.gamutWarning ? cmsFLAGS_GAMUTCHECK : 0u);
    Transform transform(cmsCreateProofingTransformTHR(
        context.id, input.get(), TYPE_RGB_16, display.get(), TYPE_RGB_16, profile.get(),
        cmsUInt32Number(settings.intent),
        settings.simulatePaper ? INTENT_ABSOLUTE_COLORIMETRIC : INTENT_RELATIVE_COLORIMETRIC, flags));
    if (!transform)
        return fail(error, context.error("Cannot construct the ICC proofing transform"));
    return true;
#else
    Q_UNUSED(settings)
    return fail(error,
                "This build has no LittleCMS backend; ICC proofing and CMYK conversion are unavailable.");
#endif
}
ProofSettings documentProofSettings(const DocumentState &state) {
    return ProofSettings::fromJson(state.metadata.value("proofing").toObject());
}
QImage softProofImage(const QImage &source, const QByteArray &sourceIcc, const ProofSettings &settings,
                      QString *error) {
    if (error)
        error->clear();
    if (!boundedImage(source, error))
        return {};
#ifdef SERIKA_HAVE_LCMS2
    CmsContext context;
    auto input = rgbProfile(context, sourceProfile(source, sourceIcc), error);
    auto output = rgbProfile(context, settings.displayProfile, error);
    auto proof = openProfile(context, settings.outputProfile, error);
    if (!input || !output || !proof || !cmykOutput(proof.get(), settings.intent, true, error))
        return {};
    cmsUInt32Number flags = cmsFLAGS_SOFTPROOFING | cmsFLAGS_COPY_ALPHA;
    if (settings.blackPointCompensation)
        flags |= cmsFLAGS_BLACKPOINTCOMPENSATION;
    if (settings.gamutWarning) {
        flags |= cmsFLAGS_GAMUTCHECK;
        cmsUInt16Number alarm[cmsMAXCHANNELS] = {};
        alarm[0] = 65535;
        alarm[2] = 65535;
        cmsSetAlarmCodesTHR(context.id, alarm);
    }
    Transform transform(cmsCreateProofingTransformTHR(
        context.id, input.get(), TYPE_RGBA_16, output.get(), TYPE_RGBA_16, proof.get(),
        cmsUInt32Number(settings.intent),
        settings.simulatePaper ? INTENT_ABSOLUTE_COLORIMETRIC : INTENT_RELATIVE_COLORIMETRIC, flags));
    if (!transform) {
        fail(error, context.error("Cannot construct the ICC proofing transform"));
        return {};
    }
    const auto pixels = source.convertToFormat(QImage::Format_RGBA64);
    QImage result(source.size(), QImage::Format_RGBA64);
    if (pixels.isNull() || result.isNull()) {
        fail(error, "Insufficient memory for the proof image.");
        return {};
    }
    for (int y = 0; y < pixels.height(); ++y)
        cmsDoTransform(transform.get(), pixels.constScanLine(y), result.scanLine(y),
                       cmsUInt32Number(pixels.width()));
    result.setColorSpace(settings.displayProfile.isEmpty()
                             ? QColorSpace(QColorSpace::SRgb)
                             : QColorSpace::fromIccProfile(settings.displayProfile));
    return result;
#else
    Q_UNUSED(sourceIcc)
    Q_UNUSED(settings)
    fail(error, "This build has no LittleCMS backend; soft proofing is unavailable.");
    return {};
#endif
}
QImage displayProofImage(const Document *document, const QImage &source, QString *error) {
    if (error)
        error->clear();
    if (!document)
        return source;
    const auto settings = documentProofSettings(document->state);
    if (!settings.enabled && !settings.gamutWarning)
        return source;
    const auto result = softProofImage(source, document->state.iccProfile, settings, error);
    return result.isNull() ? source : result;
}
CmykImage convertToCmyk(const QImage &source, const QByteArray &sourceIcc, const ProofSettings &settings,
                        int bitDepth, const QColor &paper, QString *error) {
    if (error)
        error->clear();
    if (!boundedImage(source, error))
        return {};
    if (bitDepth != 8 && bitDepth != 16) {
        fail(error, "CMYK export supports 8-bit and 16-bit integer ink channels.");
        return {};
    }
#ifdef SERIKA_HAVE_LCMS2
    CmsContext context;
    auto input = rgbProfile(context, sourceProfile(source, sourceIcc), error);
    auto output = openProfile(context, settings.outputProfile, error);
    if (!input || !output || !cmykOutput(output.get(), settings.intent, false, error))
        return {};
    const auto flags = settings.blackPointCompensation ? cmsFLAGS_BLACKPOINTCOMPENSATION : 0u;
    Transform transform(cmsCreateTransformTHR(context.id, input.get(), TYPE_RGB_16, output.get(),
                                              bitDepth == 8 ? TYPE_CMYK_8 : TYPE_CMYK_16,
                                              cmsUInt32Number(settings.intent), flags));
    if (!transform) {
        fail(error, context.error("Cannot construct the ICC separation transform"));
        return {};
    }
    CmykImage result;
    result.size = source.size();
    result.bitDepth = bitDepth;
    result.iccProfile = settings.outputProfile;
    result.profileName = profileName(output.get());
    result.intent = settings.intent;
    result.blackPointCompensation = settings.blackPointCompensation;
    result.paper = paper;
    const qsizetype rowBytes = qsizetype(source.width()) * 4 * (bitDepth / 8);
    result.samples.resize(rowBytes * source.height());
    const auto pixels = source.convertToFormat(QImage::Format_RGBA64);
    if (pixels.isNull() || result.samples.isEmpty()) {
        fail(error, "Insufficient memory for CMYK ink samples.");
        return {};
    }
    QVector<quint16> row(source.width() * 3);
    const auto paper64 = paper.rgba64();
    const quint16 background[3] = {paper64.red(), paper64.green(), paper64.blue()};
    for (int y = 0; y < source.height(); ++y) {
        const auto *rgba = reinterpret_cast<const QRgba64 *>(pixels.constScanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            const quint16 values[3] = {rgba[x].red(), rgba[x].green(), rgba[x].blue()};
            const quint64 alpha = rgba[x].alpha();
            for (int c = 0; c < 3; ++c)
                row[x * 3 + c] =
                    quint16((values[c] * alpha + background[c] * (65535 - alpha) + 32767) / 65535);
        }
        cmsDoTransform(transform.get(), row.constData(), result.samples.data() + y * rowBytes,
                       cmsUInt32Number(source.width()));
    }
    return result;
#else
    Q_UNUSED(sourceIcc)
    Q_UNUSED(settings)
    Q_UNUSED(paper)
    fail(error, "This build has no LittleCMS backend; CMYK separation is unavailable.");
    return {};
#endif
}
QString renderingIntentName(RenderingIntent intent) {
    switch (intent) {
    case RenderingIntent::Perceptual:
        return "Perceptual";
    case RenderingIntent::RelativeColorimetric:
        return "Relative Colorimetric";
    case RenderingIntent::Saturation:
        return "Saturation";
    case RenderingIntent::AbsoluteColorimetric:
        return "Absolute Colorimetric";
    }
    return "Invalid intent";
}
} // namespace serika
