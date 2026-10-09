#include "BrushDynamics.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineF>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace serika {
namespace {
qreal bounded(qreal value, qreal minimum, qreal maximum, qreal fallback) {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}
BrushInput normalizedInput(BrushInput input) {
    input.pressure = bounded(input.pressure, 0, 1, 1);
    input.tilt.setX(bounded(input.tilt.x(), -90, 90, 0));
    input.tilt.setY(bounded(input.tilt.y(), -90, 90, 0));
    return input;
}
bool validPoint(QPointF point) { return std::isfinite(point.x()) && std::isfinite(point.y()); }
void fail(QString *error, const QString &message) {
    if (error)
        *error = message;
}
} // namespace
BrushPreset BrushPreset::normalized() const {
    BrushPreset result = *this;
    result.name = name.trimmed().left(128);
    if (result.name.isEmpty())
        result.name = QStringLiteral("Round brush");
    result.size = std::clamp(size, 1, 5000);
    result.hardness = bounded(hardness, 0, 1, 0.8);
    result.opacity = bounded(opacity, 0, 1, 1);
    result.flow = bounded(flow, 0, 1, 1);
    result.spacing = bounded(spacing, 0.01, 4, 0.1);
    result.angle = std::isfinite(angle) ? std::remainder(angle, 360.0) : 0;
    result.roundness = bounded(roundness, 0.05, 1, 1);
    result.smoothing = bounded(smoothing, 0, 1, 0);
    result.sizeJitter = bounded(sizeJitter, 0, 1, 0);
    result.minimumSize = bounded(minimumSize, 0.01, 1, 0.05);
    result.opacityJitter = bounded(opacityJitter, 0, 1, 0);
    result.angleJitter = bounded(angleJitter, 0, 180, 0);
    result.roundnessJitter = bounded(roundnessJitter, 0, 1, 0);
    result.scatter = bounded(scatter, 0, 4, 0);
    result.count = std::clamp(count, 1, 16);
    result.countJitter = bounded(countJitter, 0, 1, 0);
    return result;
}
QJsonObject BrushPreset::toJson() const {
    const BrushPreset p = normalized();
    return {{"name", p.name},
            {"size", p.size},
            {"hardness", p.hardness},
            {"opacity", p.opacity},
            {"flow", p.flow},
            {"spacing", p.spacing},
            {"angle", p.angle},
            {"roundness", p.roundness},
            {"smoothing", p.smoothing},
            {"sizeJitter", p.sizeJitter},
            {"minimumSize", p.minimumSize},
            {"opacityJitter", p.opacityJitter},
            {"angleJitter", p.angleJitter},
            {"roundnessJitter", p.roundnessJitter},
            {"scatter", p.scatter},
            {"count", p.count},
            {"countJitter", p.countJitter},
            {"scatterBothAxes", p.scatterBothAxes},
            {"pressureSize", p.pressureSize},
            {"pressureOpacity", p.pressureOpacity},
            {"tiltShape", p.tiltShape},
            {"angleFollowsStroke", p.angleFollowsStroke}};
}
bool BrushPreset::fromJson(const QJsonObject &json, BrushPreset *preset, QString *error) {
    if (error)
        error->clear();
    if (!preset || !json.value("name").isString() || json.value("name").toString().trimmed().isEmpty() ||
        json.value("name").toString().size() > 128) {
        fail(error, QStringLiteral("A brush preset needs a name of 1–128 characters."));
        return false;
    }
    BrushPreset result;
    result.name = json.value("name").toString();
    const auto number = [&](const char *key, qreal minimum, qreal maximum, qreal &value) {
        if (!json.contains(QLatin1String(key)))
            return true;
        const QJsonValue field = json.value(QLatin1String(key));
        if (!field.isDouble() || !std::isfinite(field.toDouble()) || field.toDouble() < minimum ||
            field.toDouble() > maximum) {
            fail(error, QStringLiteral("Invalid value for brush setting '%1'.").arg(QLatin1String(key)));
            return false;
        }
        value = field.toDouble();
        return true;
    };
    qreal size = result.size, count = result.count;
    if (!number("size", 1, 5000, size) || !number("hardness", 0, 1, result.hardness) ||
        !number("opacity", 0, 1, result.opacity) || !number("flow", 0, 1, result.flow) ||
        !number("spacing", 0.01, 4, result.spacing) || !number("angle", -360, 360, result.angle) ||
        !number("roundness", 0.05, 1, result.roundness) || !number("smoothing", 0, 1, result.smoothing) ||
        !number("sizeJitter", 0, 1, result.sizeJitter) ||
        !number("minimumSize", 0.01, 1, result.minimumSize) ||
        !number("opacityJitter", 0, 1, result.opacityJitter) ||
        !number("angleJitter", 0, 180, result.angleJitter) ||
        !number("roundnessJitter", 0, 1, result.roundnessJitter) ||
        !number("scatter", 0, 4, result.scatter) || !number("count", 1, 16, count) ||
        !number("countJitter", 0, 1, result.countJitter) || size != std::floor(size) ||
        count != std::floor(count)) {
        if (error && error->isEmpty())
            *error = QStringLiteral("Brush size and scatter count must be integers.");
        return false;
    }
    result.size = int(size);
    result.count = int(count);
    const auto boolean = [&](const char *key, bool &value) {
        if (!json.contains(QLatin1String(key)))
            return true;
        if (!json.value(QLatin1String(key)).isBool()) {
            fail(error, QStringLiteral("Brush setting '%1' must be true or false.").arg(QLatin1String(key)));
            return false;
        }
        value = json.value(QLatin1String(key)).toBool();
        return true;
    };
    if (!boolean("scatterBothAxes", result.scatterBothAxes) ||
        !boolean("pressureSize", result.pressureSize) ||
        !boolean("pressureOpacity", result.pressureOpacity) || !boolean("tiltShape", result.tiltShape) ||
        !boolean("angleFollowsStroke", result.angleFollowsStroke))
        return false;
    *preset = result.normalized();
    if (error)
        error->clear();
    return true;
}
qreal BrushDynamics::randomUnit() {
    m_random += Q_UINT64_C(0x9E3779B97F4A7C15);
    quint64 value = m_random;
    value = (value ^ (value >> 30)) * Q_UINT64_C(0xBF58476D1CE4E5B9);
    value = (value ^ (value >> 27)) * Q_UINT64_C(0x94D049BB133111EB);
    value ^= value >> 31;
    return qreal(value >> 11) * (1.0 / 9007199254740992.0);
}
void BrushDynamics::emitGroup(const BrushInput &input, QVector<BrushDab> &output, bool smooth) {
    if (smooth && m_preset.smoothing > 0) {
        const qreal response =
            1 - std::exp(-m_interval / std::max(qreal(0.1), m_preset.size * m_preset.smoothing * 0.35));
        m_filtered += (input.position - m_filtered) * response;
    } else
        m_filtered = input.position;
    const int count = std::max(1, int(std::ceil(m_preset.count * (1 - m_preset.countJitter * randomUnit()))));
    for (int i = 0; i < count; ++i) {
        BrushDab dab;
        const qreal sizePressure = m_preset.pressureSize ? input.pressure : 1;
        const qreal sizeScale =
            std::max(m_preset.minimumSize, sizePressure * (1 - m_preset.sizeJitter * randomUnit()));
        dab.diameter = std::max(qreal(1), m_preset.size * sizeScale);
        dab.hardness = m_preset.hardness;
        dab.opacity = m_preset.opacity * (m_preset.pressureOpacity ? input.pressure : 1) *
                      (1 - m_preset.opacityJitter * randomUnit());
        dab.flow = m_preset.flow;
        dab.angle = m_preset.angle + (randomUnit() * 2 - 1) * m_preset.angleJitter;
        dab.roundness =
            std::max(qreal(0.05), m_preset.roundness * (1 - m_preset.roundnessJitter * randomUnit()));
        if (m_preset.angleFollowsStroke)
            dab.angle += std::atan2(m_tangent.y(), m_tangent.x()) * 180 / std::numbers::pi;
        if (m_preset.tiltShape && std::hypot(input.tilt.x(), input.tilt.y()) > 1) {
            dab.angle += std::atan2(input.tilt.y(), input.tilt.x()) * 180 / std::numbers::pi;
            dab.roundness *=
                std::clamp(1 - std::hypot(input.tilt.x(), input.tilt.y()) / 120.0, qreal(0.25), qreal(1));
        }
        const qreal perpendicular = (randomUnit() * 2 - 1) * m_preset.size * m_preset.scatter / 2;
        const qreal parallel =
            m_preset.scatterBothAxes ? (randomUnit() * 2 - 1) * m_preset.size * m_preset.scatter / 2 : 0;
        dab.position =
            m_filtered + m_tangent * parallel + QPointF(-m_tangent.y(), m_tangent.x()) * perpendicular;
        output.append(dab);
    }
}
QVector<BrushDab> BrushDynamics::begin(const BrushPreset &preset, const BrushInput &input, quint64 seed) {
    cancel();
    if (!validPoint(input.position))
        return {};
    m_preset = preset.normalized();
    m_previous = normalizedInput(input);
    m_filtered = input.position;
    m_random = seed;
    m_interval = std::max(qreal(0.5), m_preset.size * m_preset.spacing);
    m_untilNext = m_interval;
    m_active = true;
    QVector<BrushDab> result;
    emitGroup(m_previous, result, false);
    return result;
}
QVector<BrushDab> BrushDynamics::append(const BrushInput &rawInput) {
    QVector<BrushDab> result;
    if (!m_active || !validPoint(rawInput.position))
        return result;
    const BrushInput input = normalizedInput(rawInput);
    const QPointF delta = input.position - m_previous.position;
    const qreal distance = std::hypot(delta.x(), delta.y());
    if (distance > 1e-9) {
        m_tangent = delta / distance;
        qreal traveled = 0;
        while (distance - traveled + 1e-9 >= m_untilNext) {
            traveled += m_untilNext;
            const qreal t = std::min(qreal(1), traveled / distance);
            const BrushInput sample{m_previous.position + delta * t,
                                    m_previous.pressure + (input.pressure - m_previous.pressure) * t,
                                    m_previous.tilt + (input.tilt - m_previous.tilt) * t};
            emitGroup(sample, result);
            m_untilNext = m_interval;
        }
        m_untilNext -= std::max(qreal(0), distance - traveled);
    }
    m_previous = input;
    return result;
}
QVector<BrushDab> BrushDynamics::finish(const BrushInput &rawInput) {
    QVector<BrushDab> result = append(rawInput);
    if (!m_active || !validPoint(rawInput.position))
        return result;
    const BrushInput input = normalizedInput(rawInput);
    const QPointF start = m_filtered;
    const qreal distance = QLineF(start, input.position).length();
    if (distance > 1e-6) {
        const int count = std::max(1, int(std::ceil(distance / m_interval)));
        for (int i = 1; i <= count; ++i) {
            BrushInput sample = input;
            sample.position = start + (input.position - start) * (qreal(i) / count);
            emitGroup(sample, result, false);
        }
    }
    cancel();
    return result;
}
void BrushDynamics::cancel() {
    m_active = false;
    m_tangent = {1, 0};
}
QList<BrushPreset> builtinBrushPresets() {
    BrushPreset hard;
    hard.name = QStringLiteral("Hard Round");
    hard.hardness = 1;
    BrushPreset soft = hard;
    soft.name = QStringLiteral("Soft Airbrush");
    soft.size = 90;
    soft.hardness = 0.15;
    soft.flow = 0.15;
    BrushPreset ink = hard;
    ink.name = QStringLiteral("Fine Ink");
    ink.size = 5;
    ink.spacing = 0.12;
    BrushPreset scatter = hard;
    scatter.name = QStringLiteral("Dry Scatter");
    scatter.size = 60;
    scatter.hardness = 0.85;
    scatter.spacing = 0.18;
    scatter.sizeJitter = 0.7;
    scatter.opacityJitter = 0.4;
    scatter.scatter = 0.65;
    scatter.count = 2;
    scatter.angleJitter = 45;
    scatter.roundness = 0.4;
    BrushPreset calligraphy = hard;
    calligraphy.name = QStringLiteral("Calligraphy");
    calligraphy.size = 45;
    calligraphy.roundness = 0.18;
    calligraphy.angle = 40;
    calligraphy.spacing = 0.05;
    return {hard, soft, ink, scatter, calligraphy};
}
QList<BrushPreset> loadBrushPresets(const QString &path, QString *error) {
    if (error)
        error->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, file.errorString());
        return {};
    }
    if (file.size() > 2 * 1024 * 1024) {
        fail(error, QStringLiteral("Brush preset files must be smaller than 2 MiB."));
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        fail(error, QStringLiteral("Invalid brush preset JSON: %1").arg(parseError.errorString()));
        return {};
    }
    const QJsonObject root = json.object();
    if (root.value("format").toString() != "serika-brush-presets" || root.value("version").toInt() != 1 ||
        !root.value("presets").isArray()) {
        fail(error, QStringLiteral("Unsupported brush preset format or version."));
        return {};
    }
    const QJsonArray array = root.value("presets").toArray();
    if (array.isEmpty() || array.size() > 256) {
        fail(error, QStringLiteral("A brush library must contain 1–256 presets."));
        return {};
    }
    QList<BrushPreset> presets;
    for (const auto &value : array) {
        BrushPreset preset;
        if (!value.isObject() || !BrushPreset::fromJson(value.toObject(), &preset, error)) {
            if (error && error->isEmpty())
                *error = QStringLiteral("Invalid brush entry.");
            return {};
        }
        presets.append(preset);
    }
    return presets;
}
bool saveBrushPresets(const QString &path, const QList<BrushPreset> &presets, QString *error) {
    if (error)
        error->clear();
    if (presets.isEmpty() || presets.size() > 256) {
        fail(error, QStringLiteral("A brush library must contain 1–256 presets."));
        return false;
    }
    QJsonArray array;
    for (const BrushPreset &preset : presets)
        array.append(preset.toJson());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        fail(error, file.errorString());
        return false;
    }
    const QByteArray data =
        QJsonDocument(QJsonObject{{"format", "serika-brush-presets"}, {"version", 1}, {"presets", array}})
            .toJson();
    if (file.write(data) != data.size() || !file.commit()) {
        fail(error, file.errorString());
        return false;
    }
    return true;
}
} // namespace serika
