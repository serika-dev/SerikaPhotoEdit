#include "document/ColorManagement.h"
#include "document/Document.h"
#include "io/FormatIO.h"
#include "io/cmyk/CmykExport.h"
#include "ui/MainWindow.h"
#include <QAction>
#include <QColorSpace>
#include <QFile>
#include <QJsonDocument>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#ifdef SERIKA_HAVE_LCMS2
#include <lcms2.h>
#endif
using namespace serika;
namespace {
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QImage testImage() {
    QImage image(4, 2, QImage::Format_RGBA64);
    image.setColorSpace(QColorSpace::SRgb);
    const QColor colors[]{Qt::white,
                          Qt::black,
                          Qt::red,
                          QColor(27, 137, 241),
                          Qt::green,
                          Qt::blue,
                          QColor(96, 81, 73, 128),
                          QColor(1, 5, 9, 0)};
    for (int i = 0; i < 8; ++i)
        image.setPixelColor(i % 4, i / 4, colors[i]);
    return image;
}
struct TiffTag {
    quint16 type = 0;
    quint32 count = 0;
    quint32 value = 0;
    QByteArray data;
};
QHash<int, TiffTag> tags(const QByteArray &file) {
    QHash<int, TiffTag> result;
    if (file.size() < 10 || file.left(4) != QByteArray("II\x2a\0", 4))
        return result;
    const auto u16 = [&](qsizetype p) {
        return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(file.constData() + p));
    };
    const auto u32 = [&](qsizetype p) {
        return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(file.constData() + p));
    };
    const qsizetype ifd = u32(4);
    if (ifd > file.size() - 2)
        return result;
    const int count = u16(ifd);
    for (int i = 0; i < count; ++i) {
        const qsizetype p = ifd + 2 + i * 12;
        if (p + 12 > file.size())
            return {};
        TiffTag tag{u16(p + 2), u32(p + 4), u32(p + 8), {}};
        const qsizetype unit = tag.type == 3 ? 2 : tag.type == 4 ? 4 : tag.type == 5 ? 8 : 1;
        const quint64 size = quint64(tag.count) * unit;
        const quint64 offset = size <= 4 ? quint64(p + 8) : tag.value;
        if (offset + size > quint64(file.size()))
            return {};
        tag.data = file.mid(qsizetype(offset), qsizetype(size));
        result.insert(u16(p), tag);
    }
    return result;
}
#ifdef SERIKA_HAVE_LCMS2
QByteArray profileBytes(cmsHPROFILE profile) {
    if (!profile)
        return {};
    cmsUInt32Number size = 0;
    if (!cmsSaveProfileToMem(profile, nullptr, &size))
        return {};
    QByteArray result(int(size), Qt::Uninitialized);
    return cmsSaveProfileToMem(profile, result.data(), &size) ? result : QByteArray();
}
struct FixtureTransforms {
    cmsHTRANSFORM labToRgb;
    cmsHTRANSFORM rgbToLab;
};
cmsInt32Number labToCmyk(const cmsUInt16Number in[], cmsUInt16Number out[], void *cargo) {
    auto *transform = static_cast<FixtureTransforms *>(cargo);
    double rgb[3];
    cmsDoTransform(transform->labToRgb, in, rgb, 1);
    double ink[3];
    for (int i = 0; i < 3; ++i)
        ink[i] = 1.0 - std::clamp((rgb[i] - 0.06) / (0.86 - 0.03 * i), 0.0, 1.0);
    const double black = std::min({ink[0], ink[1], ink[2]});
    for (int i = 0; i < 3; ++i)
        out[i] = quint16(std::lround((black >= 1 ? 0.0 : (ink[i] - black) / (1 - black)) * 65535));
    out[3] = quint16(std::lround(black * 65535));
    return 1;
}
cmsInt32Number cmykToLab(const cmsUInt16Number in[], cmsUInt16Number out[], void *cargo) {
    auto *transform = static_cast<FixtureTransforms *>(cargo);
    double rgb[3];
    for (int i = 0; i < 3; ++i)
        rgb[i] = 0.06 + (0.86 - 0.03 * i) * (1 - in[i] / 65535.0) * (1 - in[3] / 65535.0);
    cmsDoTransform(transform->rgbToLab, rgb, out, 1);
    return 1;
}
// Generated test-only ICC with both real ICC LUT directions. It is deliberately
// synthetic and is never installed, offered as a printer preset, or used by the app.
QByteArray syntheticCmykProfile() {
    cmsHPROFILE rgb = cmsCreate_sRGBProfile();
    cmsHPROFILE lab = cmsCreateLab4Profile(nullptr);
    FixtureTransforms transforms{cmsCreateTransform(lab, TYPE_Lab_16, rgb, TYPE_RGB_DBL,
                                                    INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE),
                                 cmsCreateTransform(rgb, TYPE_RGB_DBL, lab, TYPE_Lab_16,
                                                    INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE)};
    cmsHPROFILE output = cmsCreateProfilePlaceholder(nullptr);
    cmsSetProfileVersion(output, 4.3);
    cmsSetDeviceClass(output, cmsSigOutputClass);
    cmsSetColorSpace(output, cmsSigCmykData);
    cmsSetPCS(output, cmsSigLabData);
    cmsSetHeaderRenderingIntent(output, INTENT_RELATIVE_COLORIMETRIC);
    const cmsCIEXYZ paper{0.88, 0.92, 0.71};
    cmsWriteTag(output, cmsSigMediaWhitePointTag, &paper);
    auto *description = cmsMLUalloc(nullptr, 1);
    cmsMLUsetASCII(description, "en", "US", "Serika synthetic CMYK TEST ONLY");
    cmsWriteTag(output, cmsSigProfileDescriptionTag, description);
    cmsMLUsetASCII(description, "en", "US", "Generated only for Serika tests; not a printer profile.");
    cmsWriteTag(output, cmsSigCopyrightTag, description);
    cmsMLUfree(description);
    auto addLut = [&](int inputs, int outputs, int grid, cmsTagSignature tag, cmsSAMPLER16 sampler) {
        auto *pipeline = cmsPipelineAlloc(nullptr, inputs, outputs);
        auto *clut = cmsStageAllocCLut16bit(nullptr, grid, inputs, outputs, nullptr);
        cmsStageSampleCLut16bit(clut, sampler, &transforms, 0);
        cmsPipelineInsertStage(pipeline, cmsAT_END, cmsStageAllocToneCurves(nullptr, inputs, nullptr));
        cmsPipelineInsertStage(pipeline, cmsAT_END, clut);
        cmsPipelineInsertStage(pipeline, cmsAT_END, cmsStageAllocToneCurves(nullptr, outputs, nullptr));
        cmsWriteTag(output, tag, pipeline);
        cmsPipelineFree(pipeline);
    };
    addLut(3, 4, 17, cmsSigBToA0Tag, labToCmyk);
    addLut(4, 3, 9, cmsSigAToB0Tag, cmykToLab);
    for (auto tag : {cmsSigBToA1Tag, cmsSigBToA2Tag})
        cmsLinkTag(output, tag, cmsSigBToA0Tag);
    for (auto tag : {cmsSigAToB1Tag, cmsSigAToB2Tag})
        cmsLinkTag(output, tag, cmsSigAToB0Tag);
    const auto bytes = profileBytes(output);
    cmsDeleteTransform(transforms.labToRgb);
    cmsDeleteTransform(transforms.rgbToLab);
    cmsCloseProfile(output);
    cmsCloseProfile(rgb);
    cmsCloseProfile(lab);
    return bytes;
}
QByteArray srgbProfile() {
    auto *profile = cmsCreate_sRGBProfile();
    const auto bytes = profileBytes(profile);
    cmsCloseProfile(profile);
    return bytes;
}
#endif
} // namespace
class ColorManagementTests : public QObject {
    Q_OBJECT
    QByteArray m_profile;
    QTemporaryDir m_settingsDirectory;
    ProofSettings settings() const {
        ProofSettings value;
        value.outputProfile = m_profile;
        value.profileName = "Serika synthetic CMYK TEST ONLY";
        value.enabled = true;
        return value;
    }
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
#ifdef SERIKA_HAVE_LCMS2
        QVERIFY(colorManagementAvailable());
        m_profile = syntheticCmykProfile();
        QVERIFY(!m_profile.isEmpty());
        QString error;
        QVERIFY2(validateProofSettings(settings(), &error), qPrintable(error));
#else
        QVERIFY(!colorManagementAvailable());
#endif
    }
    void invalidAndWrongSpaceProfiles() {
        QString error;
        QVERIFY(!inspectIccProfile("bad profile", nullptr, &error));
        QVERIFY(!error.isEmpty());
        auto value = settings();
        QVERIFY(!validateProofSettings(ProofSettings(), &error));
        QVERIFY(convertToCmyk(testImage(), {}, ProofSettings(), 8, Qt::white, &error).samples.isEmpty());
        QVERIFY(softProofImage(testImage(), {}, ProofSettings(), &error).isNull());
#ifdef SERIKA_HAVE_LCMS2
        value.outputProfile = srgbProfile();
        QVERIFY(!validateProofSettings(value, &error));
        QVERIFY(error.contains("CMYK"));
        value = settings();
        value.displayProfile = m_profile;
        QVERIFY(!validateProofSettings(value, &error));
        QVERIFY(softProofImage(testImage(), m_profile, settings(), &error).isNull());
        value = settings();
        value.intent = RenderingIntent(5);
        QVERIFY(!validateProofSettings(value, &error));
        auto truncated = m_profile.left(m_profile.size() - 1);
        QVERIFY(!inspectIccProfile(truncated, nullptr, &error));
        auto malformed = m_profile;
        malformed.replace(36, 4, "nope");
        QVERIFY(!inspectIccProfile(malformed, nullptr, &error));
        // A valid header/tag directory must not make an unreadable LUT valid.
        malformed = m_profile;
        const auto count =
            qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(malformed.constData() + 128));
        bool corrupted = false;
        for (quint32 i = 0; i < count; ++i) {
            const auto *entry = reinterpret_cast<const uchar *>(malformed.constData() + 132 + i * 12);
            if (qFromBigEndian<quint32>(entry) == cmsSigBToA0Tag) {
                const auto offset = qFromBigEndian<quint32>(entry + 4);
                malformed.replace(offset, 4, "junk");
                corrupted = true;
                break;
            }
        }
        QVERIFY(corrupted);
        value = settings();
        value.outputProfile = malformed;
        QVERIFY(!validateProofSettings(value, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(softProofImage(testImage(), {}, value, &error).isNull());
        auto *link = cmsCreateInkLimitingDeviceLink(cmsSigCmykData, 300);
        value = settings();
        value.outputProfile = profileBytes(link);
        cmsCloseProfile(link);
        QVERIFY(!validateProofSettings(value, &error));
#endif
    }
    void profileReadBoundsAndInfo() {
#ifdef SERIKA_HAVE_LCMS2
        QTemporaryDir directory;
        const auto path = directory.filePath("test.icc");
        QVERIFY(write(path, m_profile));
        QString error;
        QCOMPARE(readIccProfile(path, &error), m_profile);
        IccProfileInfo info;
        QVERIFY(inspectIccProfile(m_profile, &info, &error));
        QVERIFY(info.cmykOutput);
        QCOMPARE(info.colorSpace, QString("CMYK"));
        QVERIFY(info.name.contains("TEST ONLY"));
        QVERIFY(readIccProfile(directory.filePath("missing.icc"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(write(path, QByteArray(16 * 1024 * 1024 + 1, '\0')));
        QVERIFY(readIccProfile(path, &error).isEmpty());
#endif
    }
    void settingsAndNativeRoundtrip() {
#ifdef SERIKA_HAVE_LCMS2
        auto value = settings();
        value.displayProfile = srgbProfile();
        value.displayName = "test sRGB";
        value.intent = RenderingIntent::Saturation;
        value.simulatePaper = true;
        value.blackPointCompensation = false;
        value.gamutWarning = true;
        QCOMPARE(ProofSettings::fromJson(value.toJson()).toJson(), value.toJson());
        std::unique_ptr<Document> doc(Document::create(QSize(4, 2), Qt::white));
        doc->state.metadata["proofing"] = value.toJson();
        QTemporaryDir dir;
        QString error;
        QVERIFY2(FormatIO::saveNative(doc.get(), dir.filePath("proof.spe"), &error), qPrintable(error));
        std::unique_ptr<Document> restored(FormatIO::open(dir.filePath("proof.spe"), &error));
        QVERIFY2(restored != nullptr, qPrintable(error));
        QCOMPARE(documentProofSettings(restored->state).toJson(), value.toJson());
        auto malformed = value.toJson();
        malformed["displayProfile"] = "@@not-base64";
        QVERIFY(!validateProofSettings(ProofSettings::fromJson(malformed), &error));
        const auto before = doc->state.metadata;
        doc->mutate("Toggle proof", [&] {
            value.enabled = false;
            doc->state.metadata["proofing"] = value.toJson();
        });
        doc->undo();
        QCOMPARE(doc->state.metadata, before);
#endif
    }
    void cmykMatchesIndependentTransform_data() {
        QTest::addColumn<int>("depth");
        QTest::addColumn<int>("intent");
        QTest::addColumn<bool>("bpc");
        for (int depth : {8, 16})
            for (int intent = 0; intent < 4; ++intent)
                for (bool bpc : {false, true})
                    QTest::newRow(qPrintable(QString("%1bit-intent%2-bpc%3").arg(depth).arg(intent).arg(bpc)))
                        << depth << intent << bpc;
    }
    void cmykMatchesIndependentTransform() {
        QFETCH(int, depth);
        QFETCH(int, intent);
        QFETCH(bool, bpc);
#ifdef SERIKA_HAVE_LCMS2
        auto value = settings();
        value.intent = RenderingIntent(intent);
        value.blackPointCompensation = bpc;
        const auto source = testImage();
        QString error;
        const auto converted = convertToCmyk(source, {}, value, depth, Qt::white, &error);
        QVERIFY2(converted.isValid(), qPrintable(error));
        const auto inputBytes = source.colorSpace().iccProfile();
        auto *input = cmsOpenProfileFromMem(inputBytes.constData(), cmsUInt32Number(inputBytes.size()));
        auto *output = cmsOpenProfileFromMem(m_profile.constData(), cmsUInt32Number(m_profile.size()));
        auto *transform =
            cmsCreateTransform(input, TYPE_RGB_16, output, depth == 8 ? TYPE_CMYK_8 : TYPE_CMYK_16,
                               cmsUInt32Number(intent), bpc ? cmsFLAGS_BLACKPOINTCOMPENSATION : 0);
        QVERIFY(transform);
        QByteArray expected(converted.samples.size(), Qt::Uninitialized);
        QVector<quint16> pixels(8 * 3);
        for (int i = 0; i < 8; ++i) {
            const auto rgba = source.pixelColor(i % 4, i / 4).rgba64();
            const quint16 channels[]{rgba.red(), rgba.green(), rgba.blue()};
            for (int c = 0; c < 3; ++c)
                pixels[i * 3 + c] = quint16(
                    (quint64(channels[c]) * rgba.alpha() + quint64(65535) * (65535 - rgba.alpha()) + 32767) /
                    65535);
        }
        cmsDoTransform(transform, pixels.constData(), expected.data(), 8);
        QCOMPARE(converted.samples, expected);
        QCOMPARE(converted.iccProfile, m_profile);
        // Fully transparent colored pixels separate like white paper.
        QCOMPARE(converted.samples.mid(7 * 4 * (depth / 8), 4 * (depth / 8)),
                 converted.samples.left(4 * (depth / 8)));
        QVERIFY(converted.samples.mid(1 * 4 * (depth / 8), 4 * (depth / 8)) !=
                converted.samples.left(4 * (depth / 8)));
        cmsDeleteTransform(transform);
        cmsCloseProfile(output);
        cmsCloseProfile(input);
#else
        Q_UNUSED(depth)
        Q_UNUSED(intent)
        Q_UNUSED(bpc)
#endif
    }
    void proofMatchesIndependentTransform_data() {
        QTest::addColumn<bool>("paper");
        QTest::addColumn<bool>("gamut");
        for (bool paper : {false, true})
            for (bool gamut : {false, true})
                QTest::newRow(qPrintable(QString("paper%1-gamut%2").arg(paper).arg(gamut))) << paper << gamut;
    }
    void proofMatchesIndependentTransform() {
        QFETCH(bool, paper);
        QFETCH(bool, gamut);
#ifdef SERIKA_HAVE_LCMS2
        auto value = settings();
        value.simulatePaper = paper;
        value.gamutWarning = gamut;
        const auto source = testImage();
        QString error;
        const auto proof = softProofImage(source, {}, value, &error);
        QVERIFY2(!proof.isNull(), qPrintable(error));
        auto context = cmsCreateContext(nullptr, nullptr);
        const auto sourceBytes = source.colorSpace().iccProfile();
        auto *input =
            cmsOpenProfileFromMemTHR(context, sourceBytes.constData(), cmsUInt32Number(sourceBytes.size()));
        auto *rgb = cmsCreate_sRGBProfileTHR(context);
        auto *output =
            cmsOpenProfileFromMemTHR(context, m_profile.constData(), cmsUInt32Number(m_profile.size()));
        cmsUInt16Number alarm[cmsMAXCHANNELS] = {};
        alarm[0] = alarm[2] = 65535;
        cmsSetAlarmCodesTHR(context, alarm);
        const auto flags = cmsFLAGS_SOFTPROOFING | cmsFLAGS_COPY_ALPHA | cmsFLAGS_BLACKPOINTCOMPENSATION |
                           (gamut ? cmsFLAGS_GAMUTCHECK : 0);
        auto *transform = cmsCreateProofingTransformTHR(
            context, input, TYPE_RGBA_16, rgb, TYPE_RGBA_16, output, INTENT_RELATIVE_COLORIMETRIC,
            paper ? INTENT_ABSOLUTE_COLORIMETRIC : INTENT_RELATIVE_COLORIMETRIC, flags);
        QVERIFY(transform);
        QImage expected(source.size(), QImage::Format_RGBA64);
        for (int y = 0; y < source.height(); ++y)
            cmsDoTransform(transform, source.constScanLine(y), expected.scanLine(y), 4);
        for (int y = 0; y < source.height(); ++y) {
            QCOMPARE(QByteArray(reinterpret_cast<const char *>(proof.constScanLine(y)), 32),
                     QByteArray(reinterpret_cast<const char *>(expected.constScanLine(y)), 32));
            for (int x = 0; x < source.width(); ++x)
                QCOMPARE(proof.pixelColor(x, y).rgba64().alpha(), source.pixelColor(x, y).rgba64().alpha());
        }
        QVERIFY(proof != source);
        if (gamut) {
            bool marked = false;
            for (int y = 0; y < proof.height(); ++y)
                for (int x = 0; x < proof.width(); ++x)
                    marked |= proof.pixelColor(x, y).red() == 255 && proof.pixelColor(x, y).green() == 0 &&
                              proof.pixelColor(x, y).blue() == 255;
            QVERIFY(marked);
        }
        cmsDeleteTransform(transform);
        cmsCloseProfile(output);
        cmsCloseProfile(input);
        cmsCloseProfile(rgb);
        cmsDeleteContext(context);
#else
        Q_UNUSED(paper)
        Q_UNUSED(gamut)
#endif
    }
    void displayDoesNotChangeDocument() {
        std::unique_ptr<Document> doc(Document::create(QSize(4, 2), Qt::white));
        auto image = testImage();
        QString error;
        QCOMPARE(displayProofImage(doc.get(), image, &error).cacheKey(), image.cacheKey());
        const auto original = doc->state.layers[0].pixels.image();
#ifdef SERIKA_HAVE_LCMS2
        doc->state.metadata["proofing"] = settings().toJson();
        QVERIFY(displayProofImage(doc.get(), image, &error) != image);
        QCOMPARE(doc->state.layers[0].pixels.image(), original);
        auto value = settings();
        value.outputProfile = "broken";
        doc->state.metadata["proofing"] = value.toJson();
        QCOMPARE(displayProofImage(doc.get(), image, &error).cacheKey(), image.cacheKey());
        QVERIFY(!error.isEmpty());
#endif
    }
    void canvasCachesProofAndGamutWithoutChangingPixels() {
#ifdef SERIKA_HAVE_LCMS2
        std::unique_ptr<Document> doc(Document::create({32, 24}, Qt::red));
        CanvasView canvas(doc.get());
        canvas.resize(400, 300);
        canvas.setShowRulers(false);
        canvas.setShowGrid(false);
        canvas.setShowGuides(false);
        canvas.setTool("Move");
        canvas.show();
        QCoreApplication::processEvents();
        canvas.setZoom(4);
        const auto original = doc->composite();
        const auto sourceKey = original.cacheKey();
        const auto snapshot = doc->state.layers[0].pixels.image();
        const auto screenshotColor = [&] {
            QImage screenshot(canvas.size(), QImage::Format_RGBA8888);
            screenshot.fill(Qt::transparent);
            QPainter painter(&screenshot);
            canvas.render(&painter);
            painter.end();
            return screenshot.pixelColor(canvas.fromDocument({8, 8}).toPoint());
        };
        QCOMPARE(screenshotColor(), QColor(Qt::red));
        auto value = settings();
        doc->state.metadata["proofing"] = value.toJson();
        const auto expected = softProofImage(original, doc->state.iccProfile, value).pixelColor(8, 8);
        const auto proofColor = screenshotColor();
        QVERIFY(std::abs(proofColor.red() - expected.red()) <= 1);
        QVERIFY(std::abs(proofColor.green() - expected.green()) <= 1);
        QVERIFY(std::abs(proofColor.blue() - expected.blue()) <= 1);
        QCOMPARE(screenshotColor(), proofColor); // Same source/settings reuses the proof.
        value.enabled = false;
        value.gamutWarning = true;
        doc->state.metadata["proofing"] = value.toJson();
        QCOMPARE(screenshotColor(), QColor(Qt::magenta)); // Gamut display independently enables proof.
        QCOMPARE(doc->composite().cacheKey(), sourceKey); // Metadata-only display change.
        QCOMPARE(doc->state.layers[0].pixels.image(), snapshot);
        value.gamutWarning = false;
        doc->state.metadata["proofing"] = value.toJson();
        QCOMPARE(screenshotColor(), QColor(Qt::red));
        value.enabled = true;
        value.outputProfile = "invalid";
        doc->state.metadata["proofing"] = value.toJson();
        QCOMPARE(screenshotColor(), QColor(Qt::red));
        QVERIFY(!canvas.property("proofError").toString().isEmpty());
        value.enabled = false;
        doc->state.metadata["proofing"] = value.toJson();
        QCOMPARE(screenshotColor(), QColor(Qt::red));
        QVERIFY(canvas.property("proofError").toString().isEmpty());
#endif
    }
    void menusFollowTogglesUndoAndDocumentSwitch() {
#ifdef SERIKA_HAVE_LCMS2
        MainWindow window;
        window.openDemo();
        auto *first = window.currentDocument();
        first->state.metadata["proofing"] = settings().toJson();
        first->clearHistory();
        const auto action = [&](const QString &text) {
            for (auto *item : window.findChildren<QAction *>())
                if (item->text() == text)
                    return item;
            return static_cast<QAction *>(nullptr);
        };
        auto *proof = action("Proof Colors");
        auto *gamut = action("Gamut Warning");
        QVERIFY(proof);
        QVERIFY(gamut);
        window.runCommand("Proof Colors");
        QVERIFY(!documentProofSettings(first->state).enabled);
        QVERIFY(!proof->isChecked());
        window.runCommand("Undo");
        QCoreApplication::processEvents();
        QVERIFY(documentProofSettings(first->state).enabled);
        QTRY_VERIFY(proof->isChecked());
        window.runCommand("Gamut Warning");
        QVERIFY(documentProofSettings(first->state).gamutWarning);
        QVERIFY(gamut->isChecked());
        window.openDemo();
        auto *second = window.currentDocument();
        QVERIFY(second != first);
        QTRY_VERIFY(!proof->isChecked());
        QTRY_VERIFY(!gamut->isChecked());
        auto *tabs = window.findChild<QTabWidget *>();
        QVERIFY(tabs);
        tabs->setCurrentIndex(0);
        QTRY_COMPARE(window.currentDocument(), first);
        QTRY_VERIFY(proof->isChecked());
        QTRY_VERIFY(gamut->isChecked());
        first->markSaved();
        second->markSaved();
#endif
    }
    void trueCmykTiffTagsAndPixels_data() {
        QTest::addColumn<int>("depth");
        QTest::newRow("8-bit") << 8;
        QTest::newRow("16-bit") << 16;
    }
    void trueCmykTiffTagsAndPixels() {
        QFETCH(int, depth);
#ifdef SERIKA_HAVE_LCMS2
        const auto image = convertToCmyk(testImage(), {}, settings(), depth);
        QTemporaryDir directory;
        const auto path = directory.filePath("CMYK.tif");
        QString error;
        QVERIFY2(writeCmykTiff(path, image, 144, &error), qPrintable(error));
        const auto file = read(path);
        const auto values = tags(file);
        QCOMPARE(values.value(262).value, quint32(5));
        QCOMPARE(values.value(277).value, quint32(4));
        QCOMPARE(values.value(332).value, quint32(1));
        QCOMPARE(values.value(334).value, quint32(4));
        QCOMPARE(values.value(258).count, quint32(4));
        QCOMPARE(
            qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(values.value(258).data.constData())),
            quint16(depth));
        QCOMPARE(values.value(34675).data, m_profile);
        QCOMPARE(file.mid(values.value(273).value, values.value(279).value),
                 image.samples); // Host is little-endian.
        const auto before = file;
        auto broken = image;
        broken.samples.chop(1);
        QVERIFY(!writeCmykTiff(path, broken, 144, &error));
        QCOMPARE(read(path), before);
        QVERIFY(!writeCmykTiff(directory.filePath("bad.tif"), image, std::nan(""), &error));
#else
        Q_UNUSED(depth)
#endif
    }
    void separationCoverageAndManifest() {
#ifdef SERIKA_HAVE_LCMS2
        const auto image = convertToCmyk(testImage(), {}, settings(), 16);
        QTemporaryDir directory;
        const auto target = directory.filePath("Plates");
        QString error;
        QVERIFY2(writeCmykSeparations(target, image, 300, &error), qPrintable(error));
        QCOMPARE(read(target + "/output.icc"), m_profile);
        const auto manifest = QJsonDocument::fromJson(read(target + "/manifest.json")).object();
        QCOMPARE(manifest.value("bitDepth").toInt(), 16);
        QCOMPARE(manifest.value("channels").toArray().size(), 4);
        QCOMPARE(manifest.value("intent").toString(), renderingIntentName(settings().intent));
        const QStringList names{"Cyan", "Magenta", "Yellow", "Black"};
        for (int c = 0; c < 4; ++c) {
            const auto file = read(target + '/' + names[c] + ".tif");
            const auto values = tags(file);
            QCOMPARE(values.value(262).value, quint32(1));
            QCOMPARE(values.value(277).value, quint32(1));
            QVERIFY(!values.contains(34675));
            const auto samples = file.mid(values.value(273).value, values.value(279).value);
            const auto ink = image.channelSamples(c);
            for (qsizetype i = 0; i < samples.size(); i += 2)
                QCOMPARE(qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(samples.constData() + i)),
                         quint16(65535 - qFromLittleEndian<quint16>(
                                             reinterpret_cast<const uchar *>(ink.constData() + i))));
        }
        QVERIFY(!writeCmykSeparations(target, image, 300, &error));
        QVERIFY(!writeCmykSeparations(directory.filePath("Invalid"), image, 0, &error));
        QVERIFY(!QFileInfo::exists(directory.filePath("Invalid")));
        QVERIFY(!writeCmykSeparations(directory.filePath("Missing/Child"), image, 300, &error));
#endif
    }
    void invalidDimensionsAndDepth() {
        CmykImage image;
        image.size = QSize(2147483647, 2147483647);
        image.iccProfile = "bad";
        QVERIFY(!image.isValid());
        QVERIFY(image.channelSamples(0).isEmpty());
        QString error;
        QVERIFY(convertToCmyk(QImage(), {}, settings(), 8, Qt::white, &error).samples.isEmpty());
        QVERIFY(convertToCmyk(testImage(), {}, settings(), 32, Qt::white, &error).samples.isEmpty());
        QVERIFY(!error.isEmpty());
    }
};
QTEST_MAIN(ColorManagementTests)
#include "color_management_tests.moc"
