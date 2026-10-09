#pragma once
#include "tools/BrushDynamics.h"
#include <QWidget>
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
namespace serika {
class BrushSettingsWidget final : public QWidget {
    Q_OBJECT
  public:
    explicit BrushSettingsWidget(QWidget *parent = nullptr);
    BrushPreset preset() const { return m_preset; }
    void setPreset(const BrushPreset &preset);
    QList<BrushPreset> presets() const { return m_presets; }
    bool importPresets(const QString &path, QString *error = nullptr);
    bool exportPresets(const QString &path, QString *error = nullptr) const;
    bool saveCurrentPreset(const QString &name, QString *error = nullptr);

  signals:
    void presetChanged(const serika::BrushPreset &preset);
    void presetsChanged();

  private:
    struct ScalarBinding {
        QDoubleSpinBox *control;
        qreal BrushPreset::*field;
        qreal scale;
    };
    struct BooleanBinding {
        QCheckBox *control;
        bool BrushPreset::*field;
    };
    BrushPreset m_preset;
    QList<BrushPreset> m_presets;
    QVector<ScalarBinding> m_scalars;
    QVector<BooleanBinding> m_booleans;
    QComboBox *m_picker = nullptr;
    QSpinBox *m_size = nullptr;
    QSpinBox *m_count = nullptr;
    QLabel *m_status = nullptr;
    QWidget *m_preview = nullptr;
    bool m_updating = false;
    void changed();
    void refreshPicker();
    bool persist(QString *error);
};
} // namespace serika
