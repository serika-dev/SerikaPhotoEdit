#pragma once
#include "document/Document.h"
#include <QDialog>
class QTextEdit;
class QFontComboBox;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QLabel;
class QTextCharFormat;
namespace serika {
class TypeEditorDialog : public QDialog {
    Q_OBJECT
  public:
    TypeEditorDialog(const Layer &layer, QSize canvas, QWidget *parent = nullptr);
    Layer editedLayer() const;
    void setTextPath(const QPainterPath &path);
  signals:
    void previewChanged();

  private:
    Layer m_original;
    Layer m_layer;
    QSize m_canvas;
    bool m_dirty = false;
    bool m_syncing = false;
    QTextEdit *m_editor = nullptr;
    QWidget *m_preview = nullptr;
    QFontComboBox *m_family = nullptr;
    QDoubleSpinBox *m_size = nullptr;
    QDoubleSpinBox *m_tracking = nullptr;
    QDoubleSpinBox *m_baseline = nullptr;
    QCheckBox *m_bold = nullptr;
    QCheckBox *m_italic = nullptr;
    QCheckBox *m_underline = nullptr;
    QCheckBox *m_kerning = nullptr;
    QComboBox *m_alignment = nullptr;
    QDoubleSpinBox *m_leading = nullptr;
    QDoubleSpinBox *m_before = nullptr;
    QDoubleSpinBox *m_after = nullptr;
    QDoubleSpinBox *m_left = nullptr;
    QDoubleSpinBox *m_right = nullptr;
    QDoubleSpinBox *m_first = nullptr;
    QComboBox *m_layout = nullptr;
    QDoubleSpinBox *m_width = nullptr;
    QDoubleSpinBox *m_pathOffset = nullptr;
    QCheckBox *m_reverse = nullptr;
    QLabel *m_pathLabel = nullptr;
    void syncControls();
    void applyCharacterFormat(const QTextCharFormat &format);
    void applyParagraphFormat();
    void changed();
    void updatePreview();
};
} // namespace serika
