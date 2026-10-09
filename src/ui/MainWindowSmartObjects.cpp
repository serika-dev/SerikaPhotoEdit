#include "MainWindow.h"
#include "document/SmartObjectOperations.h"
#include "io/FormatIO.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QTabWidget>
namespace serika {
namespace {
struct ContentsBinding {
    QPointer<Document> parent;
    quint64 layerId = 0;
    QString link;
    QByteArray sourceSnapshot;
    QByteArray linkedFingerprint;
};
// Document tabs can detach into another MainWindow. Bindings follow the contents document.
QHash<const Document *, ContentsBinding> &bindings() {
    static auto *registry = new QHash<const Document *, ContentsBinding>;
    return *registry;
}
QByteArray fingerprint(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const auto data = file.read(1024 * 1024);
        if (data.isEmpty() && file.error() != QFile::NoError)
            return {};
        hash.addData(data);
    }
    return hash.result();
}
} // namespace
bool MainWindow::isSmartObjectContents(const Document *document) const {
    return bindings().contains(document);
}
bool MainWindow::saveSmartObjectContents(Document *contents, bool exportCopy) {
    auto it = bindings().find(contents);
    if (it == bindings().end())
        return false;
    QString error;
    if (exportCopy) {
        auto path = QFileDialog::getSaveFileName(this, "Export a copy of contents", contents->title + ".spe",
                                                 FormatIO::saveFilter());
        if (path.isEmpty())
            return false;
        if (QFileInfo(path).suffix().isEmpty())
            path += ".spe";
        if (!FormatIO::save(contents, path, &error)) {
            QMessageBox::warning(this, "Export contents", error);
            return false;
        }
        showMessage("Exported a copy. Save applies contents to the parent Smart Object.");
        return true;
    }
    const auto binding = it.value();
    auto *parent = binding.parent.data();
    const int index = parent ? parent->indexForId(binding.layerId) : -1;
    if (index < 0) {
        QMessageBox::warning(
            this, "Save contents",
            "The parent document or layer was closed. Save As can export your contents to a separate file.");
        return false;
    }
    const auto &layer = parent->state.layers[index];
    if (layer.linkedPath != binding.link || layer.embeddedDocument != binding.sourceSnapshot) {
        QMessageBox::warning(this, "Save contents",
                             "The parent contents were replaced or relinked while this tab was open. Reopen "
                             "Edit Contents, or export this tab with Save As.");
        return false;
    }
    bool ok;
    if (!layer.linkedPath.isEmpty()) {
        const auto path = smartObjectLinkedPath(parent, layer);
        if (binding.linkedFingerprint.isEmpty() || fingerprint(path) != binding.linkedFingerprint) {
            QMessageBox::warning(this, "Save linked contents",
                                 "The linked original changed or disappeared on disk. Reopen Edit Contents "
                                 "before saving, or export your work with Save As.");
            return false;
        }
        const auto suffix = QFileInfo(path).suffix().toLower();
        if (contents->state.layers.size() > 1 && !QStringList{"spe", "speb", "psd", "psb"}.contains(suffix)) {
            if (QMessageBox::question(
                    this, "Save linked contents",
                    "Saving updates “" + path +
                        "” with flattened pixels. The parent keeps an editable native snapshot. Continue?",
                    QMessageBox::Save | QMessageBox::Cancel) != QMessageBox::Save)
                return false;
        }
        ok = saveLinkedSmartObjectContents(parent, binding.layerId, contents, &error);
    } else
        ok = applySmartObjectContents(parent, binding.layerId, contents, &error);
    if (!ok) {
        QMessageBox::warning(this, "Save contents", error);
        return false;
    }
    it = bindings().find(contents);
    it->sourceSnapshot = parent->state.layers[parent->indexForId(binding.layerId)].embeddedDocument;
    if (!it->link.isEmpty())
        it->linkedFingerprint = fingerprint(smartObjectLinkedPath(parent, parent->state.layers[index]));
    contents->markSaved();
    refresh();
    showMessage(binding.link.isEmpty()
                    ? "Updated the parent Smart Object. Save the parent document to keep it."
                    : "Saved the linked original and updated the parent Smart Object.");
    return true;
}
bool MainWindow::runSmartObjectCommand(const QString &name) {
    const QStringList names{"Place Embedded...",         "Place Linked...",       "Convert to Smart Object",
                            "Convert for Smart Filters", "Edit Contents...",      "Replace Contents...",
                            "Relink to File...",         "Update Linked Content", "Embed Linked",
                            "Export Contents..."};
    if (!names.contains(name))
        return false;
    auto *document = currentDocument();
    if (!document) {
        showMessage("Open a parent document first.");
        return true;
    }
    QString error;
    if (name == "Place Embedded..." || name == "Place Linked...") {
        const auto path = QFileDialog::getOpenFileName(this, name, {}, FormatIO::openFilter());
        if (!path.isEmpty() && !placeSmartObject(document, path, name == "Place Linked...", &error))
            QMessageBox::warning(this, "Place Smart Object", error);
        return true;
    }
    const auto *layer = document->activeLayer();
    if (!layer) {
        showMessage("Select a layer.");
        return true;
    }
    const auto id = layer->id;
    if (name == "Convert to Smart Object" || name == "Convert for Smart Filters") {
        if (!convertLayerToEmbeddedSmartObject(document, id, &error))
            QMessageBox::warning(this, "Convert Smart Object", error);
        return true;
    }
    if (layer->kind != LayerKind::SmartObject) {
        showMessage("Select a Smart Object layer.");
        return true;
    }
    if (name == "Edit Contents...") {
        for (auto it = bindings().cbegin(); it != bindings().cend(); ++it)
            if (it->parent == document && it->layerId == id)
                for (int tab = 0; tab < m_tabs->count(); ++tab)
                    if (auto *canvas = m_tabs->widget(tab)->findChild<CanvasView *>())
                        if (canvas->document() == it.key()) {
                            m_tabs->setCurrentIndex(tab);
                            return true;
                        }
        if (layer->locked) {
            showMessage("The Smart Object layer is locked.");
            return true;
        }
        auto *contents = openSmartObjectContents(document, id, &error, this);
        if (!contents) {
            QMessageBox::warning(this, "Edit contents", error);
            return true;
        }
        const auto link = layer->linkedPath;
        bindings().insert(
            contents, {document, id, link, layer->embeddedDocument,
                       link.isEmpty() ? QByteArray() : fingerprint(smartObjectLinkedPath(document, *layer))});
        connect(contents, &QObject::destroyed, qApp, [contents] { bindings().remove(contents); });
        contents->title = layer->name + (link.isEmpty() ? " · Embedded Contents" : " · Linked Contents");
        contents->filePath.clear();
        addDocument(contents);
        showMessage(link.isEmpty() ? "Save updates the parent Smart Object; Save As exports a copy."
                                   : "Save writes the linked original; Save As exports a copy.");
        return true;
    }
    bool ok = true;
    if (name == "Replace Contents..." || name == "Relink to File...") {
        const auto path = QFileDialog::getOpenFileName(this, name, smartObjectLinkedPath(document, *layer),
                                                       FormatIO::openFilter());
        if (path.isEmpty())
            return true;
        ok = name == "Replace Contents..." ? replaceSmartObjectContents(document, id, path, &error)
                                           : relinkSmartObject(document, id, path, &error);
    } else if (name == "Update Linked Content")
        ok = reloadSmartObject(document, id, &error);
    else if (name == "Embed Linked")
        ok = embedSmartObject(document, id, &error);
    else if (name == "Export Contents...") {
        auto path = QFileDialog::getSaveFileName(this, "Export retained contents", layer->name + ".spe",
                                                 FormatIO::saveFilter());
        if (path.isEmpty())
            return true;
        if (QFileInfo(path).suffix().isEmpty())
            path += ".spe";
        ok = exportSmartObjectContents(document, id, path, &error);
    }
    if (!ok)
        QMessageBox::warning(this, "Smart Object contents", error);
    return true;
}
} // namespace serika
