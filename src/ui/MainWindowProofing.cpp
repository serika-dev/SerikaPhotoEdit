#include "MainWindow.h"
#include "document/ColorManagement.h"
#include "io/cmyk/CmykExport.h"
#include "ui/dialogs/ProofSetupDialog.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

namespace serika {
bool MainWindow::runProofingCommand(const QString &name) {
    if (name == "CMYK")
        return runProofingCommand("Proof Setup...");
    const QStringList commands{"Proof Setup...", "Proof Colors", "Gamut Warning", "Export CMYK TIFF...",
                               "Export CMYK Separations..."};
    if (!commands.contains(name))
        return false;
    auto *document = currentDocument();
    if (!document) {
        showMessage("Open an RGB document to set up a CMYK proof or export.");
        return true;
    }
    if (!colorManagementAvailable()) {
        const auto current = documentProofSettings(document->state);
        if (name == "Proof Colors" && m_commands.value(name))
            m_commands.value(name)->setChecked(current.enabled);
        if (name == "Gamut Warning" && m_commands.value(name))
            m_commands.value(name)->setChecked(current.gamutWarning);
        QMessageBox::warning(
            this, "Color Management",
            "This build has no LittleCMS backend. ICC proofing and CMYK conversion are unavailable.");
        return true;
    }
    auto settings = documentProofSettings(document->state);
    const bool toggle = name == "Proof Colors" || name == "Gamut Warning";
    const bool desired = name == "Proof Colors" ? !settings.enabled : !settings.gamutWarning;
    QString error;
    if (name == "Proof Setup..." || ((!toggle || desired) && !validateProofSettings(settings, &error))) {
        ProofSetupDialog dialog(document, this);
        if (dialog.exec() != QDialog::Accepted) {
            if (toggle && m_commands.value(name))
                m_commands.value(name)->setChecked(!desired);
            return true;
        }
        settings = dialog.settings();
        document->mutate("Proof Setup", [=] { document->state.metadata["proofing"] = settings.toJson(); });
        if (name == "Proof Setup...") {
            refresh();
            showMessage("CMYK proof profile: " + settings.profileName);
            return true;
        }
    }
    if (toggle) {
        if (name == "Proof Colors")
            settings.enabled = desired;
        else
            settings.gamutWarning = desired;
        document->mutate(name, [=] { document->state.metadata["proofing"] = settings.toJson(); });
        if (m_commands.value(name))
            m_commands.value(name)->setChecked(desired);
        refresh();
        showMessage((settings.enabled || settings.gamutWarning ? "CMYK proof: " : "Proof disabled: ") +
                    settings.profileName + "; " + renderingIntentName(settings.intent));
        return true;
    }
    bool accepted = false;
    const auto depth =
        QInputDialog::getItem(this, "CMYK Export", "Ink channel precision", {"8-bit", "16-bit"},
                              document->state.bitDepth > 8 ? 1 : 0, false, &accepted);
    if (!accepted)
        return true;
    QString destination;
    if (name == "Export CMYK TIFF...") {
        destination = QFileDialog::getSaveFileName(this, "Export ICC-separated CMYK TIFF",
                                                   document->title + "-CMYK.tif", "CMYK TIFF (*.tif *.tiff)");
        if (!destination.isEmpty() && QFileInfo(destination).suffix().isEmpty())
            destination += ".tif";
    } else {
        const auto parent =
            QFileDialog::getExistingDirectory(this, "Choose the parent directory for new ink separations");
        if (parent.isEmpty())
            return true;
        const auto folder =
            QInputDialog::getText(this, "CMYK Separations", "New directory name", QLineEdit::Normal,
                                  QFileInfo(document->title).completeBaseName() + "-CMYK", &accepted);
        if (!accepted)
            return true;
        if (folder.isEmpty() || folder == "." || folder == ".." || folder.contains('/') ||
            folder.contains('\\') || folder.contains(':')) {
            QMessageBox::warning(this, "CMYK Separations", "Enter a single, new directory name.");
            return true;
        }
        destination = QDir(parent).filePath(folder);
    }
    if (destination.isEmpty())
        return true;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto image = convertToCmyk(document->composite(), document->state.iccProfile, settings,
                                     depth == "16-bit" ? 16 : 8, Qt::white, &error);
    const bool ok = image.isValid() &&
                    (name == "Export CMYK TIFF..."
                         ? writeCmykTiff(destination, image, document->state.resolution, &error)
                         : writeCmykSeparations(destination, image, document->state.resolution, &error));
    QApplication::restoreOverrideCursor();
    if (!ok)
        QMessageBox::warning(this, "CMYK Export", error);
    else
        showMessage("Exported CMYK inks using " + image.profileName + ": " + destination);
    return true;
}
} // namespace serika
