#include "BrushSettingsWidget.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace serika {
namespace {
QString libraryPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/brush-presets.json";
}
class BrushPreview final : public QWidget {
  public:
    explicit BrushPreview(QWidget *parent) : QWidget(parent) {
        setMinimumHeight(86);
        setMaximumHeight(100);
        setToolTip(
            tr("Sample stroke with pen pressure varying from 25% to 100%. Large tips are scaled to fit."));
    }
    void setPreset(const BrushPreset &preset) {
        m_preset = preset;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Base));
        const QSize size = this->size();
        if (size.width() < 25 || size.height() < 25)
            return;
        BrushPreset preset = m_preset.normalized();
        preset.size = std::min(preset.size, 48);
        BrushDynamics dynamics;
        QVector<BrushDab> dabs;
        const auto inputAt = [&](qreal x) {
            const qreal t = (x - 12) / std::max(1, size.width() - 24);
            return BrushInput{{x, size.height() / 2.0 + std::sin(t * std::numbers::pi * 2) * 11},
                              0.25 + 0.75 * std::sin(t * std::numbers::pi),
                              {25, 15}};
        };
        dabs += dynamics.begin(preset, inputAt(12), 314159);
        for (int x = 14; x < size.width() - 12; x += 2)
            dabs += dynamics.append(inputAt(x));
        dabs += dynamics.finish(inputAt(size.width() - 12));
        QVector<qreal> coverage(size.width() * size.height(), 0);
        for (const BrushDab &dab : dabs) {
            const qreal radius = dab.diameter / 2;
            const QRect bounds =
                QRectF(dab.position - QPointF(radius, radius), QSizeF(radius * 2, radius * 2))
                    .toAlignedRect()
                    .intersected(rect());
            const qreal radians = dab.angle * std::numbers::pi / 180;
            const qreal cosine = std::cos(radians), sine = std::sin(radians);
            for (int y = bounds.top(); y <= bounds.bottom(); ++y)
                for (int x = bounds.left(); x <= bounds.right(); ++x) {
                    const qreal dx = x + 0.5 - dab.position.x(), dy = y + 0.5 - dab.position.y();
                    const qreal bx = dx * cosine + dy * sine;
                    const qreal by = (-dx * sine + dy * cosine) / dab.roundness;
                    const qreal distance = std::hypot(bx, by) / std::max(qreal(0.5), radius);
                    if (distance >= 1)
                        continue;
                    const qreal edge =
                        distance <= dab.hardness
                            ? 1
                            : std::pow((1 - distance) / std::max(qreal(0.001), 1 - dab.hardness), 2);
                    qreal &alpha = coverage[y * size.width() + x];
                    alpha += std::max(qreal(0), dab.opacity - alpha) * edge * dab.flow;
                }
        }
        QImage image(size, QImage::Format_RGBA8888);
        image.fill(Qt::transparent);
        QColor color = palette().color(QPalette::Highlight);
        for (int y = 0; y < size.height(); ++y)
            for (int x = 0; x < size.width(); ++x) {
                color.setAlphaF(std::clamp(coverage[y * size.width() + x], qreal(0), qreal(1)));
                image.setPixelColor(x, y, color);
            }
        painter.drawImage(0, 0, image);
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }

  private:
    BrushPreset m_preset;
};
} // namespace
BrushSettingsWidget::BrushSettingsWidget(QWidget *parent)
    : QWidget(parent), m_presets(builtinBrushPresets()) {
    qRegisterMetaType<BrushPreset>();
    setObjectName("brushSettings");
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);
    m_picker = new QComboBox;
    m_picker->setObjectName("brushPresetPicker");
    m_picker->setPlaceholderText(tr("Current brush"));
    m_picker->setToolTip(tr("Apply a saved brush preset."));
    outer->addWidget(m_picker);
    m_preview = new BrushPreview(this);
    m_preview->setObjectName("brushStrokePreview");
    outer->addWidget(m_preview);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    const auto group = [&](const QString &title) {
        auto *box = new QGroupBox(title);
        auto *form = new QFormLayout(box);
        form->setContentsMargins(8, 16, 8, 8);
        form->setSpacing(5);
        layout->addWidget(box);
        return form;
    };
    const auto scalar = [&](QFormLayout *form, const QString &label, const char *name, qreal minimum,
                            qreal maximum, qreal scale, qreal BrushPreset::*field, const QString &suffix) {
        auto *control = new QDoubleSpinBox;
        control->setObjectName(QLatin1String(name));
        control->setRange(minimum, maximum);
        control->setDecimals(1);
        control->setSingleStep(1);
        control->setSuffix(suffix);
        m_scalars.append({control, field, scale});
        form->addRow(label, control);
        connect(control, &QDoubleSpinBox::valueChanged, this, [this, field, scale](double value) {
            if (!m_updating) {
                m_preset.*field = value / scale;
                changed();
            }
        });
        return control;
    };
    const auto boolean = [&](QFormLayout *form, const QString &label, const char *name,
                             bool BrushPreset::*field) {
        auto *control = new QCheckBox(label);
        control->setObjectName(QLatin1String(name));
        m_booleans.append({control, field});
        form->addRow(control);
        connect(control, &QCheckBox::toggled, this, [this, field](bool checked) {
            if (!m_updating) {
                m_preset.*field = checked;
                changed();
            }
        });
    };
    auto *tip = group(tr("Brush tip"));
    m_size = new QSpinBox;
    m_size->setObjectName("brushPresetSize");
    m_size->setRange(1, 5000);
    m_size->setSuffix(tr(" px"));
    tip->addRow(tr("Size"), m_size);
    connect(m_size, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_updating) {
            m_preset.size = value;
            changed();
        }
    });
    scalar(tip, tr("Hardness"), "hardness", 0, 100, 100, &BrushPreset::hardness, "%");
    scalar(tip, tr("Spacing"), "spacing", 1, 400, 100, &BrushPreset::spacing, "%")
        ->setToolTip(tr("Distance between dabs as a percentage of the nominal brush diameter."));
    scalar(tip, tr("Angle"), "angle", -180, 180, 1, &BrushPreset::angle, tr("°"));
    scalar(tip, tr("Roundness"), "roundness", 5, 100, 100, &BrushPreset::roundness, "%");
    scalar(tip, tr("Smoothing"), "smoothing", 0, 100, 100, &BrushPreset::smoothing, "%");
    auto *transfer = group(tr("Transfer"));
    scalar(transfer, tr("Opacity"), "opacity", 0, 100, 100, &BrushPreset::opacity, "%")
        ->setToolTip(tr("Maximum coverage in a single gesture."));
    scalar(transfer, tr("Flow"), "flow", 0, 100, 100, &BrushPreset::flow, "%")
        ->setToolTip(tr("Coverage added by each dab, within the gesture opacity limit."));
    auto *dynamics = group(tr("Shape dynamics"));
    scalar(dynamics, tr("Size jitter"), "sizeJitter", 0, 100, 100, &BrushPreset::sizeJitter, "%");
    scalar(dynamics, tr("Minimum size"), "minimumSize", 1, 100, 100, &BrushPreset::minimumSize, "%");
    scalar(dynamics, tr("Opacity jitter"), "opacityJitter", 0, 100, 100, &BrushPreset::opacityJitter, "%");
    scalar(dynamics, tr("Angle jitter"), "angleJitter", 0, 180, 1, &BrushPreset::angleJitter, tr("°"));
    scalar(dynamics, tr("Roundness jitter"), "roundnessJitter", 0, 100, 100, &BrushPreset::roundnessJitter,
           "%");
    auto *scatter = group(tr("Scattering"));
    scalar(scatter, tr("Spread"), "scatter", 0, 400, 100, &BrushPreset::scatter, "%")
        ->setToolTip(tr("Spread around the stroke as a percentage of the nominal brush diameter."));
    m_count = new QSpinBox;
    m_count->setObjectName("scatterCount");
    m_count->setRange(1, 16);
    scatter->addRow(tr("Count"), m_count);
    connect(m_count, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_updating) {
            m_preset.count = value;
            changed();
        }
    });
    scalar(scatter, tr("Count jitter"), "countJitter", 0, 100, 100, &BrushPreset::countJitter, "%");
    boolean(scatter, tr("Both axes"), "scatterBothAxes", &BrushPreset::scatterBothAxes);
    auto *controls = group(tr("Pen and direction"));
    boolean(controls, tr("Pressure controls size"), "pressureSize", &BrushPreset::pressureSize);
    boolean(controls, tr("Pressure controls opacity"), "pressureOpacity", &BrushPreset::pressureOpacity);
    boolean(controls, tr("Tilt controls tip shape"), "tiltShape", &BrushPreset::tiltShape);
    boolean(controls, tr("Angle follows stroke"), "angleFollowsStroke", &BrushPreset::angleFollowsStroke);
    layout->addStretch();
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    auto *actions = new QHBoxLayout;
    auto *save = new QPushButton(tr("Save preset"));
    auto *importButton = new QPushButton(tr("Import"));
    auto *exportButton = new QPushButton(tr("Export"));
    exportButton->setToolTip(tr("Export all saved presets as a Serika brush JSON library."));
    actions->addWidget(save);
    actions->addWidget(importButton);
    actions->addWidget(exportButton);
    outer->addLayout(actions);
    m_status = new QLabel;
    m_status->setObjectName("muted");
    m_status->setWordWrap(true);
    outer->addWidget(m_status);
    connect(m_picker, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_updating || index < 0 || index >= m_presets.size())
            return;
        setPreset(m_presets[index]);
        changed();
    });
    connect(save, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save brush preset"), tr("Name"),
                                                   QLineEdit::Normal, m_preset.name, &ok);
        if (!ok)
            return;
        QString error;
        const bool saved = saveCurrentPreset(name, &error);
        m_status->setText(saved ? tr("Preset saved on this device.") : error);
    });
    connect(importButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Import brush presets"), {},
                                                          tr("Serika brush presets (*.sbrush.json *.json)"));
        if (path.isEmpty())
            return;
        QString error;
        const bool imported = importPresets(path, &error);
        m_status->setText(imported ? tr("Presets imported.") : error);
    });
    connect(exportButton, &QPushButton::clicked, this, [this] {
        QString path = QFileDialog::getSaveFileName(this, tr("Export brush presets"), "brushes.sbrush.json",
                                                    tr("Serika brush presets (*.sbrush.json)"));
        if (path.isEmpty())
            return;
        if (!path.endsWith(".json", Qt::CaseInsensitive))
            path += ".sbrush.json";
        QString error;
        const bool exported = exportPresets(path, &error);
        m_status->setText(exported ? tr("Preset library exported.") : error);
    });
    if (QFileInfo::exists(libraryPath())) {
        QString error;
        const QList<BrushPreset> saved = loadBrushPresets(libraryPath(), &error);
        if (!saved.isEmpty())
            m_presets = saved;
        else
            m_status->setText(error);
    }
    refreshPicker();
    setPreset(m_preset);
}
void BrushSettingsWidget::setPreset(const BrushPreset &preset) {
    m_updating = true;
    m_preset = preset.normalized();
    m_size->setValue(m_preset.size);
    m_count->setValue(m_preset.count);
    for (const ScalarBinding &binding : m_scalars)
        binding.control->setValue(m_preset.*(binding.field) * binding.scale);
    for (const BooleanBinding &binding : m_booleans)
        binding.control->setChecked(m_preset.*(binding.field));
    m_picker->setCurrentIndex(m_picker->findText(m_preset.name));
    static_cast<BrushPreview *>(m_preview)->setPreset(m_preset);
    m_updating = false;
}
void BrushSettingsWidget::changed() {
    m_preset = m_preset.normalized();
    static_cast<BrushPreview *>(m_preview)->setPreset(m_preset);
    emit presetChanged(m_preset);
}
void BrushSettingsWidget::refreshPicker() {
    const QSignalBlocker block(m_picker);
    m_picker->clear();
    for (const BrushPreset &preset : m_presets)
        m_picker->addItem(preset.name);
    m_picker->setCurrentIndex(m_picker->findText(m_preset.name));
}
bool BrushSettingsWidget::persist(QString *error) {
    if (!QDir().mkpath(QFileInfo(libraryPath()).absolutePath())) {
        if (error)
            *error = tr("Could not create the local brush preset folder.");
        return false;
    }
    return saveBrushPresets(libraryPath(), m_presets, error);
}
bool BrushSettingsWidget::saveCurrentPreset(const QString &name, QString *error) {
    if (name.trimmed().isEmpty() || name.size() > 128) {
        if (error)
            *error = tr("Choose a preset name of 1–128 characters.");
        return false;
    }
    BrushPreset saved = m_preset;
    saved.name = name.trimmed();
    int index = -1;
    for (int i = 0; i < m_presets.size(); ++i)
        if (m_presets[i].name == saved.name) {
            index = i;
            break;
        }
    if (index < 0 && m_presets.size() >= 256) {
        if (error)
            *error = tr("The library already contains 256 presets.");
        return false;
    }
    const auto before = m_presets;
    if (index < 0)
        m_presets.append(saved);
    else
        m_presets[index] = saved;
    if (!persist(error)) {
        m_presets = before;
        return false;
    }
    refreshPicker();
    setPreset(saved);
    changed();
    emit presetsChanged();
    return true;
}
bool BrushSettingsWidget::importPresets(const QString &path, QString *error) {
    const QList<BrushPreset> imported = loadBrushPresets(path, error);
    if (imported.isEmpty())
        return false;
    const auto before = m_presets;
    for (const BrushPreset &preset : imported) {
        int index = -1;
        for (int i = 0; i < m_presets.size(); ++i)
            if (m_presets[i].name == preset.name) {
                index = i;
                break;
            }
        if (index < 0)
            m_presets.append(preset);
        else
            m_presets[index] = preset;
    }
    if (m_presets.size() > 256 || !persist(error)) {
        if (m_presets.size() > 256 && error)
            *error = tr("The merged library would exceed 256 presets.");
        m_presets = before;
        return false;
    }
    refreshPicker();
    setPreset(imported.first());
    changed();
    emit presetsChanged();
    return true;
}
bool BrushSettingsWidget::exportPresets(const QString &path, QString *error) const {
    return saveBrushPresets(path, m_presets, error);
}
} // namespace serika
