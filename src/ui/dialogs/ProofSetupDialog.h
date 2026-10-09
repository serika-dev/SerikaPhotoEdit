#pragma once
#include "document/ColorManagement.h"
#include <QDialog>
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
namespace serika {
class Document;
class ProofSetupDialog : public QDialog {
  public:
    explicit ProofSetupDialog(const Document *document, QWidget *parent = nullptr);
    ProofSettings settings() const;

  private:
    ProofSettings m_settings;
    QImage m_source;
    QByteArray m_sourceIcc;
    QLabel *m_outputName = nullptr;
    QLabel *m_displayName = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_status = nullptr;
    QComboBox *m_intent = nullptr;
    QCheckBox *m_bpc = nullptr;
    QCheckBox *m_paper = nullptr;
    QCheckBox *m_enabled = nullptr;
    QCheckBox *m_gamut = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    void chooseProfile(bool display);
    void updatePreview();
};
} // namespace serika
