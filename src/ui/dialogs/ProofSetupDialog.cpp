#include "ProofSetupDialog.h"
#include "document/Document.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace serika {
ProofSetupDialog::ProofSetupDialog(const Document *document, QWidget *parent) : QDialog(parent) {
    setWindowTitle("CMYK Proof Setup");
    setMinimumWidth(600);
    if (document) {
        m_settings = documentProofSettings(document->state);
        m_source =
            document->composite().scaled(QSize(560, 260), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_sourceIcc = document->state.iccProfile;
    }
    if (m_settings.outputProfile.isEmpty())
        m_settings.enabled = true;
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel("Choose the ICC profile supplied for your printer, press, paper and ink. "
                             "The editable document stays in RGB; proofing previews the output process.",
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *form = new QFormLayout;
    m_outputName = new QLabel(
        m_settings.profileName.isEmpty() ? "No CMYK output profile selected" : m_settings.profileName, this);
    m_outputName->setWordWrap(true);
    auto *chooseOutput = new QPushButton("Choose CMYK ICC...", this);
    auto *outputRow = new QHBoxLayout;
    outputRow->addWidget(m_outputName, 1);
    outputRow->addWidget(chooseOutput);
    form->addRow("Output process", outputRow);
    m_displayName = new QLabel(m_settings.displayProfile.isEmpty() ? "sRGB" : m_settings.displayName, this);
    m_displayName->setWordWrap(true);
    auto *chooseDisplay = new QPushButton("Choose Display ICC...", this);
    auto *resetDisplay = new QPushButton("sRGB", this);
    auto *displayRow = new QHBoxLayout;
    displayRow->addWidget(m_displayName, 1);
    displayRow->addWidget(chooseDisplay);
    displayRow->addWidget(resetDisplay);
    form->addRow("Display profile", displayRow);
    m_intent = new QComboBox(this);
    for (int intent = 0; intent < 4; ++intent)
        m_intent->addItem(renderingIntentName(RenderingIntent(intent)), intent);
    m_intent->setCurrentIndex(qBound(0, int(m_settings.intent), 3));
    form->addRow("Rendering intent", m_intent);
    m_bpc = new QCheckBox("Compensate for the output black point", this);
    m_bpc->setChecked(m_settings.blackPointCompensation);
    form->addRow(m_bpc);
    m_paper = new QCheckBox("Simulate paper color (absolute colorimetric display proof)", this);
    m_paper->setChecked(m_settings.simulatePaper);
    form->addRow(m_paper);
    m_enabled = new QCheckBox("Proof Colors on the canvas", this);
    m_enabled->setChecked(m_settings.enabled);
    form->addRow(m_enabled);
    m_gamut = new QCheckBox("Mark colors outside the output gamut in magenta", this);
    m_gamut->setChecked(m_settings.gamutWarning);
    form->addRow(m_gamut);
    layout->addLayout(form);
    m_preview = new QLabel(this);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(170);
    layout->addWidget(m_preview);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(m_buttons);
    connect(chooseOutput, &QPushButton::clicked, this, [this] { chooseProfile(false); });
    connect(chooseDisplay, &QPushButton::clicked, this, [this] { chooseProfile(true); });
    connect(resetDisplay, &QPushButton::clicked, this, [this] {
        m_settings.displayProfile.clear();
        m_settings.displayName.clear();
        m_displayName->setText("sRGB");
        updatePreview();
    });
    connect(m_intent, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    for (auto *check : {m_bpc, m_paper, m_enabled, m_gamut})
        connect(check, &QCheckBox::toggled, this, [this] { updatePreview(); });
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttons, &QDialogButtonBox::accepted, this, [this] {
        QString error;
        if (!validateProofSettings(settings(), &error)) {
            QMessageBox::warning(this, "Proof Setup", error);
            return;
        }
        accept();
    });
    updatePreview();
}
ProofSettings ProofSetupDialog::settings() const {
    auto result = m_settings;
    result.intent = RenderingIntent(m_intent->currentData().toInt());
    result.blackPointCompensation = m_bpc->isChecked();
    result.simulatePaper = m_paper->isChecked();
    result.enabled = m_enabled->isChecked();
    result.gamutWarning = m_gamut->isChecked();
    return result;
}
void ProofSetupDialog::chooseProfile(bool display) {
    const auto path = QFileDialog::getOpenFileName(
        this, display ? "Choose RGB display ICC profile" : "Choose CMYK output ICC profile", {},
        "ICC profiles (*.icc *.icm);;All files (*)");
    if (path.isEmpty())
        return;
    QString error;
    auto bytes = readIccProfile(path, &error);
    IccProfileInfo info;
    if (bytes.isEmpty() || !inspectIccProfile(bytes, &info, &error)) {
        QMessageBox::warning(this, "ICC Profile", error);
        return;
    }
    if ((display && (info.colorSpace != "RGB" || info.deviceClass == "link")) ||
        (!display && !info.cmykOutput)) {
        QMessageBox::warning(this, "ICC Profile",
                             display ? "Choose an RGB display profile."
                                     : "Choose a genuine CMYK output/printer profile.");
        return;
    }
    const auto name = info.name.isEmpty() ? QFileInfo(path).fileName() : info.name;
    if (display) {
        m_settings.displayProfile = bytes;
        m_settings.displayName = name;
        m_displayName->setText(name);
    } else {
        m_settings.outputProfile = bytes;
        m_settings.profileName = name;
        m_outputName->setText(name);
    }
    updatePreview();
}
void ProofSetupDialog::updatePreview() {
    if (m_settings.outputProfile.isEmpty()) {
        m_preview->clear();
        m_preview->setText("A proof preview will appear here.");
        m_status->setText("Choose a printer or press profile to begin.");
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        return;
    }
    QString error;
    const auto result = softProofImage(m_source, m_sourceIcc, settings(), &error);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!result.isNull());
    if (result.isNull()) {
        m_preview->clear();
        m_preview->setText("Choose a valid output profile to preview the proof.");
        m_status->setText(error);
        return;
    }
    m_preview->setPixmap(QPixmap::fromImage(result));
    m_status->setText(colorManagementVersion() + "; " + settings().profileName +
                      ". Export flattens transparency over white. Proof accuracy depends on the profiles and "
                      "a calibrated display.");
}
} // namespace serika
