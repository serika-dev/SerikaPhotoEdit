#pragma once
#include "document/Document.h"
#include <QDialog>
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
namespace serika {
class FillEditorDialog final : public QDialog {
  public:
    FillEditorDialog(LayerKind kind, const QJsonObject &parameters, QColor foreground, QColor background,
                     QWidget *parent = nullptr, bool gradientMap = false);
    QJsonObject parameters() const;

  private:
    LayerKind m_kind;
    bool m_gradientMap = false;
    QJsonObject m_parameters;
    QGradientStops m_stops;
    QComboBox *m_style = nullptr;
    QDoubleSpinBox *m_angle, *m_scale, *m_position = nullptr, *m_opacity = nullptr;
    QCheckBox *m_reverse = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_preview;
    void refreshPreview();
    void refreshStops();
};
} // namespace serika
