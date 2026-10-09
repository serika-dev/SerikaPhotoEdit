#pragma once
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QPointF>
#include <QString>
#include <QVector>

namespace serika {
struct BrushPreset {
    QString name = QStringLiteral("Round brush");
    int size = 40;
    qreal hardness = 0.8;
    qreal opacity = 1;
    qreal flow = 1;
    qreal spacing = 0.1;
    qreal angle = 0;
    qreal roundness = 1;
    qreal smoothing = 0;
    qreal sizeJitter = 0;
    qreal minimumSize = 0.05;
    qreal opacityJitter = 0;
    qreal angleJitter = 0;
    qreal roundnessJitter = 0;
    qreal scatter = 0;
    int count = 1;
    qreal countJitter = 0;
    bool scatterBothAxes = false;
    bool pressureSize = true;
    bool pressureOpacity = true;
    bool tiltShape = false;
    bool angleFollowsStroke = false;

    BrushPreset normalized() const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &json, BrushPreset *preset, QString *error = nullptr);
};
struct BrushInput {
    QPointF position;
    qreal pressure = 1;
    QPointF tilt{};
};
struct BrushDab {
    QPointF position;
    qreal diameter = 40;
    qreal hardness = 0.8;
    qreal opacity = 1;
    qreal flow = 1;
    qreal angle = 0;
    qreal roundness = 1;
};
// Samples at fixed distances along the stroke, carrying spacing across input events.
// The explicit seed makes a gesture repeatable independently of event subdivision.
class BrushDynamics {
  public:
    QVector<BrushDab> begin(const BrushPreset &preset, const BrushInput &input, quint64 seed);
    QVector<BrushDab> append(const BrushInput &input);
    QVector<BrushDab> finish(const BrushInput &input);
    void cancel();
    bool active() const { return m_active; }

  private:
    BrushPreset m_preset;
    BrushInput m_previous;
    QPointF m_filtered;
    QPointF m_tangent{1, 0};
    quint64 m_random = 0;
    qreal m_interval = 1;
    qreal m_untilNext = 1;
    bool m_active = false;
    qreal randomUnit();
    void emitGroup(const BrushInput &input, QVector<BrushDab> &output, bool smooth = true);
};
QList<BrushPreset> builtinBrushPresets();
QList<BrushPreset> loadBrushPresets(const QString &path, QString *error = nullptr);
bool saveBrushPresets(const QString &path, const QList<BrushPreset> &presets, QString *error = nullptr);
} // namespace serika
Q_DECLARE_METATYPE(serika::BrushPreset)
