#pragma once
#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QString>

namespace serika {
class Document;
struct DocumentState;
enum class RenderingIntent {
    Perceptual = 0,
    RelativeColorimetric = 1,
    Saturation = 2,
    AbsoluteColorimetric = 3
};
struct IccProfileInfo {
    QString name;
    QString colorSpace;
    QString deviceClass;
    bool cmykOutput = false;
};
struct ProofSettings {
    QByteArray outputProfile;
    QByteArray displayProfile; // Empty means sRGB; otherwise a user-selected RGB display ICC.
    QString profileName;
    QString displayName;
    RenderingIntent intent = RenderingIntent::RelativeColorimetric;
    bool blackPointCompensation = true;
    bool simulatePaper = false;
    bool enabled = false;
    bool gamutWarning = false;
    QJsonObject toJson() const;
    static ProofSettings fromJson(const QJsonObject &json);
};
// Integer samples are native-endian, interleaved C,M,Y,K; 0 means no ink and the
// maximum means 100% ink. The profile describes the actual separation transform.
struct CmykImage {
    QSize size;
    int bitDepth = 8;
    QByteArray samples;
    QByteArray iccProfile;
    QString profileName;
    RenderingIntent intent = RenderingIntent::RelativeColorimetric;
    bool blackPointCompensation = true;
    QColor paper = Qt::white;
    bool isValid() const;
    QByteArray channelSamples(int channel) const;
};
bool colorManagementAvailable();
QString colorManagementVersion();
bool inspectIccProfile(const QByteArray &profile, IccProfileInfo *info, QString *error = nullptr);
QByteArray readIccProfile(const QString &path, QString *error = nullptr);
bool validateProofSettings(const ProofSettings &settings, QString *error = nullptr);
ProofSettings documentProofSettings(const DocumentState &state);
QImage softProofImage(const QImage &source, const QByteArray &sourceIcc, const ProofSettings &settings,
                      QString *error = nullptr);
// Returns the original image when proofing is disabled or fails; error explains a failure.
QImage displayProofImage(const Document *document, const QImage &source, QString *error = nullptr);
CmykImage convertToCmyk(const QImage &source, const QByteArray &sourceIcc, const ProofSettings &settings,
                        int bitDepth = 8, const QColor &paper = Qt::white, QString *error = nullptr);
QString renderingIntentName(RenderingIntent intent);
} // namespace serika
