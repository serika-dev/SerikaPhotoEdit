#include "MainWindow.h"
#include "panels/BrushSettingsWidget.h"
#include <QJsonDocument>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolBar>
namespace serika {
void MainWindow::syncBrushSettings(const BrushPreset &preset) {
    if (m_brushSettings)
        m_brushSettings->setPreset(preset);
    const QHash<QString, int> values{{"brushSize", preset.size},
                                     {"hardness", qRound(preset.hardness * 100)},
                                     {"brushOpacity", qRound(preset.opacity * 100)},
                                     {"flow", qRound(preset.flow * 100)}};
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        if (auto *spin = m_options->findChild<QSpinBox *>(it.key())) {
            QSignalBlocker block(spin);
            spin->setValue(it.value());
        }
    m_settings.setValue("brush/currentPreset", QJsonDocument(preset.toJson()).toJson(QJsonDocument::Compact));
}
void MainWindow::buildBrushSettings() {
    m_brushSettings = new BrushSettingsWidget;
    m_brushSettings->setObjectName("brushSettings");
    BrushPreset preset;
    BrushPreset::fromJson(
        QJsonDocument::fromJson(m_settings.value("brush/currentPreset").toByteArray()).object(), &preset);
    syncBrushSettings(preset);
    dock("Brush Settings", m_brushSettings);
    connect(m_brushSettings, &BrushSettingsWidget::presetChanged, this, [this](const BrushPreset &value) {
        syncBrushSettings(value);
        if (auto *canvas = currentCanvas())
            canvas->setBrushPreset(value);
    });
    for (const auto &key : QStringList{"brushSize", "hardness", "brushOpacity", "flow"})
        connect(m_options->findChild<QSpinBox *>(key), &QSpinBox::valueChanged, this, [this, key](int value) {
            if (currentCanvas())
                return; // Canvas signals synchronize changes while a document is open.
            auto preset = m_brushSettings->preset();
            if (key == "brushSize")
                preset.size = value;
            else if (key == "hardness")
                preset.hardness = value / 100.;
            else if (key == "brushOpacity")
                preset.opacity = value / 100.;
            else
                preset.flow = value / 100.;
            syncBrushSettings(preset);
        });
}
} // namespace serika
