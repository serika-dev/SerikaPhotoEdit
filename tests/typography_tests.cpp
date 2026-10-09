#include "document/LayerOperations.h"
#include "document/TextLayout.h"
#include "io/FormatIO.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/TypeEditorDialog.h"
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetricsF>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextFragment>
#include <QTimer>
#include <QtTest>
#include <memory>
using namespace serika;
namespace {
Layer richLayer() {
    Layer layer;
    layer.id = 5;
    layer.kind = LayerKind::Text;
    layer.name = "Editable typography";
    layer.font = QFont("Arial", 24);
    layer.color = Qt::black;
    layer.text = "HELLO world\nSecond paragraph";
    layer.parameters = {
        {"typography", QJsonObject{{"version", 1},
                                   {"layout", "paragraph"},
                                   {"width", 400},
                                   {"spans", QJsonArray{QJsonObject{{"start", 0},
                                                                    {"length", 5},
                                                                    {"size", 36},
                                                                    {"bold", true},
                                                                    {"color", "#ffff0000"}},
                                                        QJsonObject{{"start", 6},
                                                                    {"length", 5},
                                                                    {"size", 16},
                                                                    {"italic", true},
                                                                    {"color", "#ff0000ff"},
                                                                    {"tracking", 2},
                                                                    {"kerning", false}}}},
                                   {"paragraphs", QJsonArray{QJsonObject{{"position", 0},
                                                                         {"alignment", "left"},
                                                                         {"leading", 60},
                                                                         {"after", 15},
                                                                         {"leftIndent", 12}},
                                                             QJsonObject{{"position", 12},
                                                                         {"alignment", "right"},
                                                                         {"before", 8},
                                                                         {"firstIndent", 4}}}}}}};
    return layer;
}
QImage painted(const Layer &layer, QSize size = QSize(500, 260)) {
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    paintTextLayer(p, layer, size);
    return image;
}
QRect alphaBounds(const QImage &image) {
    QRect bounds;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y).alpha() > 0)
                bounds = bounds.united(QRect(x, y, 1, 1));
    return bounds;
}
} // namespace
class TypographyTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary;
    std::unique_ptr<Document> makeDocument(Layer layer) {
        auto document = std::unique_ptr<Document>(Document::create(QSize(500, 260), Qt::transparent, 16));
        document->state.layers = {layer};
        document->state.activeIndex = 0;
        document->touch();
        document->clearHistory();
        document->markSaved();
        return document;
    }
    QString saveFixture(Document *document, const QString &name) {
        const QString file = temporary.filePath(name + ".spe");
        QString error;
        if (!FormatIO::saveNative(document, file, &error))
            qFatal("Fixture save failed: %s", qPrintable(error));
        return file;
    }
  private slots:
    void initTestCase() {
        QVERIFY(temporary.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    }
    void legacyPlainRenderingRemainsExact_data() {
        QTest::addColumn<bool>("vertical");
        QTest::addColumn<QString>("alignment");
        QTest::newRow("left") << false << QString("left");
        QTest::newRow("center") << false << QString("center");
        QTest::newRow("right") << false << QString("right");
        QTest::newRow("legacy-justify-falls-back-left") << false << QString("justify");
        QTest::newRow("vertical") << true << QString("left");
    }
    void legacyPlainRenderingRemainsExact() {
        QFETCH(bool, vertical);
        QFETCH(QString, alignment);
        Layer layer;
        layer.kind = LayerKind::Text;
        layer.font = QFont("Arial", 22);
        layer.text = "Legacy A\nBé 😀";
        layer.color = QColor(70, 20, 180, 190);
        layer.parameters = {{"vertical", vertical}, {"alignment", alignment}, {"rotation", 7}};
        const QImage actual = painted(layer);
        QImage expected(actual.size(), actual.format());
        expected.fill(Qt::transparent);
        QPainter p(&expected);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.rotate(7);
        p.setFont(layer.font);
        p.setPen(layer.color);
        if (vertical) {
            const QFontMetricsF metrics(layer.font);
            qreal x = 0, y = 0;
            for (char32_t code : layer.text.toUcs4()) {
                if (code == '\n') {
                    x += metrics.height();
                    y = 0;
                    continue;
                }
                p.drawText(QPointF(x, y + metrics.ascent()), QString::fromUcs4(&code, 1));
                y += metrics.height();
            }
        } else
            p.drawText(QRectF(QPointF(), expected.size()),
                       (alignment == "center"  ? Qt::AlignHCenter
                        : alignment == "right" ? Qt::AlignRight
                                               : Qt::AlignLeft) |
                           Qt::AlignTop | Qt::TextWordWrap,
                       layer.text);
        p.end();
        QCOMPARE(actual, expected);
    }
    void spansRenderMixedSizesStylesAndColors() {
        const auto layer = richLayer();
        QTextDocument text;
        populateTextDocument(text, layer, QSize(500, 260));
        QTextCursor cursor(&text);
        cursor.setPosition(2);
        QVERIFY(cursor.charFormat().font().bold());
        QCOMPARE(cursor.charFormat().fontPointSize(), 36.0);
        cursor.setPosition(8);
        QVERIFY(cursor.charFormat().font().italic());
        QCOMPARE(cursor.charFormat().fontPointSize(), 16.0);
        QVERIFY(!cursor.charFormat().fontKerning());
        const auto image = painted(layer);
        int red = 0, blue = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const QColor c = image.pixelColor(x, y);
                if (c.alpha() > 150) {
                    if (c.red() > 200 && c.blue() < 50)
                        ++red;
                    if (c.blue() > 200 && c.red() < 50)
                        ++blue;
                }
            }
        QVERIFY(red > 100);
        QVERIFY(blue > 30);
        QVERIFY(red > blue);
    }
    void paragraphsHaveActualAlignmentLeadingSpacingAndIndents() {
        QTextDocument text;
        populateTextDocument(text, richLayer(), QSize(500, 260));
        const QTextBlock first = text.begin(), second = first.next();
        QVERIFY(second.isValid());
        QCOMPARE(first.blockFormat().leftMargin(), 12.0);
        QCOMPARE(first.blockFormat().lineHeight(), 60.0);
        QCOMPARE(first.blockFormat().bottomMargin(), 15.0);
        QCOMPARE(second.blockFormat().topMargin(), 8.0);
        QVERIFY(second.blockFormat().alignment().testFlag(Qt::AlignRight));
        QVERIFY(text.documentLayout()->blockBoundingRect(second).top() >= 60);
    }
    void documentFormattingRoundTripPreservesNativeRendering() {
        Layer layer = richLayer();
        QTextDocument text;
        populateTextDocument(text, layer, QSize(500, 260));
        QTextCursor cursor(&text);
        cursor.setPosition(6);
        cursor.setPosition(11, QTextCursor::KeepAnchor);
        QTextCharFormat format;
        format.setBaselineOffset(25);
        format.setFontStretch(135);
        format.setFontCapitalization(QFont::SmallCaps);
        cursor.mergeCharFormat(format);
        serializeTextDocument(layer, text);
        QTextDocument reopened;
        populateTextDocument(reopened, layer, QSize(500, 260));
        QTextCursor check(&reopened);
        check.setPosition(8);
        QCOMPARE(check.charFormat().baselineOffset(), 25.0);
        QCOMPARE(check.charFormat().fontStretch(), 135);
        QCOMPARE(check.charFormat().fontCapitalization(), QFont::SmallCaps);
        Layer roundTrip = layer;
        serializeTextDocument(roundTrip, reopened);
        QCOMPARE(painted(roundTrip), painted(layer));
    }
    void trackingChangesActualGlyphPositions() {
        Layer layer = richLayer();
        layer.text = "AAAA";
        auto t = layer.parameters["typography"].toObject();
        t["layout"] = "point";
        t["paragraphs"] = QJsonArray();
        t["spans"] = QJsonArray{QJsonObject{{"start", 0}, {"length", 4}, {"size", 24}, {"tracking", 0}}};
        layer.parameters["typography"] = t;
        const qreal before = textLayerBounds(layer, QSize(500, 260)).width();
        t["spans"] = QJsonArray{QJsonObject{{"start", 0}, {"length", 4}, {"size", 24}, {"tracking", 8}}};
        layer.parameters["typography"] = t;
        QVERIFY(textLayerBounds(layer, QSize(500, 260)).width() > before + 20);
    }
    void defaultFontStretchDisplaysAndRoundTripsAtNormalWidth() {
        Layer layer;
        layer.kind = LayerKind::Text;
        layer.font = QFont("Arial", 24);
        layer.font.setStretch(QFont::AnyStretch);
        layer.text = "Normal width";
        layer.parameters["typography"] = QJsonObject{{"layout", "point"}};
        TypeEditorDialog dialog(layer, QSize(500, 260));
        QCOMPARE(dialog.findChild<QDoubleSpinBox *>("typeStretch")->value(), 100.0);
        const QImage before = painted(layer);
        const QRectF boundsBefore = textLayerBounds(layer, QSize(500, 260));
        QTextDocument text;
        populateTextDocument(text, layer, QSize(500, 260));
        serializeTextDocument(layer, text);
        const auto span = layer.parameters["typography"].toObject()["spans"].toArray().first().toObject();
        QCOMPARE(span["stretch"].toInt(), 100);
        QVERIFY(!span.contains("spacingPercent"));
        QCOMPARE(painted(layer), before);
        QCOMPARE(textLayerBounds(layer, QSize(500, 260)), boundsBefore);
        // Accept older/native span data that explicitly stores Qt's AnyStretch sentinel.
        auto typography = layer.parameters["typography"].toObject();
        auto spans = typography["spans"].toArray();
        auto legacySpan = spans[0].toObject();
        legacySpan["stretch"] = 0;
        legacySpan["spacingPercent"] = 0;
        spans[0] = legacySpan;
        typography["spans"] = spans;
        layer.parameters["typography"] = typography;
        QCOMPARE(painted(layer), before);
    }
    void textOnCubicPathBendsAndRetainsEditableSource() {
        Layer layer = richLayer();
        layer.text = "CURVED TEXT";
        auto typography = layer.parameters["typography"].toObject();
        typography["spans"] = QJsonArray();
        typography["paragraphs"] = QJsonArray();
        typography["layout"] = "point";
        QPainterPath path;
        path.moveTo(20, 140);
        path.cubicTo(130, 20, 250, 20, 440, 140);
        typography["path"] = textPathToJson(path);
        typography["pathOffset"] = 8;
        layer.parameters["typography"] = typography;
        QCOMPARE(textPathFromJson(textPathToJson(path)), path);
        const auto bounds = textLayerBounds(layer, QSize(500, 260));
        QVERIFY(bounds.height() > 35);
        QVERIFY(bounds.top() > 0);
        QVERIFY(bounds.left() > 10);
        const QImage normal = painted(layer);
        QVERIFY(!alphaBounds(normal).isEmpty());
        typography["pathReverse"] = true;
        layer.parameters["typography"] = typography;
        QVERIFY(painted(layer) != normal);
        QCOMPARE(layer.text, QString("CURVED TEXT"));
    }
    void invalidPathAndLiteralHtmlCannotLoadResources() {
        QVERIFY(textPathFromJson(QJsonArray{QJsonArray{2, 1, 2}}).isEmpty());
        Layer layer = richLayer();
        layer.text = "<img src=\"file:///secret.png\">";
        QTextDocument text;
        populateTextDocument(text, layer, QSize(500, 260));
        QCOMPARE(text.toPlainText(), layer.text);
        for (QTextBlock b = text.begin(); b.isValid(); b = b.next())
            for (auto it = b.begin(); !it.atEnd(); ++it)
                QVERIFY(!it.fragment().charFormat().isImageFormat());
    }
    void sharedBoundsAgreeWithRenderedRichContent() {
        const Layer layer = richLayer();
        const QRect raster = alphaBounds(painted(layer));
        const QRectF outline = textLayerBounds(layer, QSize(500, 260));
        QVERIFY(std::abs(outline.left() - raster.left()) < 3);
        QVERIFY(std::abs(outline.top() - raster.top()) < 3);
        QVERIFY(std::abs(outline.right() - raster.right()) < 3);
        QVERIFY(std::abs(outline.bottom() - raster.bottom()) < 3);
        auto document = makeDocument(layer);
        QCOMPARE(layerContentBounds(document.get(), layer.id), outline);
    }
    void nativeAndSerikaPsdRetainRichEditableState() {
        auto document = makeDocument(richLayer());
        const QImage before = document->composite();
        const QString native = saveFixture(document.get(), "rich-native");
        QString error;
        std::unique_ptr<Document> opened(FormatIO::openNative(native, &error));
        QVERIFY2(opened, qPrintable(error));
        QCOMPARE(opened->activeLayer()->parameters, document->activeLayer()->parameters);
        QCOMPARE(opened->composite(), before);
        const QString psd = temporary.filePath("rich.psd");
        QVERIFY2(FormatIO::savePsd(document.get(), psd, &error), qPrintable(error));
        opened.reset(FormatIO::openPsd(psd, &error));
        QVERIFY2(opened, qPrintable(error));
        const Layer *textLayer = nullptr;
        for (const Layer &l : opened->state.layers)
            if (l.kind == LayerKind::Text)
                textLayer = &l;
        QVERIFY(textLayer);
        QCOMPARE(textLayer->text, document->activeLayer()->text);
        QCOMPARE(textLayer->parameters, document->activeLayer()->parameters);
    }
    void unchangedDialogRetainsLegacyAndRichState() {
        Layer legacy;
        legacy.kind = LayerKind::Text;
        legacy.text = "Unchanged";
        legacy.font = QFont("Arial", 19);
        TypeEditorDialog oldEditor(legacy, QSize(500, 260));
        QCOMPARE(oldEditor.editedLayer().parameters, legacy.parameters);
        const Layer rich = richLayer();
        TypeEditorDialog editor(rich, QSize(500, 260));
        QCOMPARE(editor.editedLayer().parameters, rich.parameters);
    }
    void dialogFormatsOnlySelectedSpanAndReopens() {
        Layer layer = richLayer();
        TypeEditorDialog dialog(layer, QSize(500, 260));
        auto *editor = dialog.findChild<QTextEdit *>("typeTextEditor");
        QTextCursor cursor = editor->textCursor();
        cursor.setPosition(6);
        cursor.setPosition(11, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        QSignalSpy preview(&dialog, &TypeEditorDialog::previewChanged);
        dialog.findChild<QDoubleSpinBox *>("typeFontSize")->setValue(28);
        dialog.findChild<QDoubleSpinBox *>("typeTracking")->setValue(3);
        QVERIFY(preview.count() >= 2);
        const Layer edited = dialog.editedLayer();
        QCOMPARE(edited.text, layer.text);
        TypeEditorDialog reopened(edited, QSize(500, 260));
        auto *text = reopened.findChild<QTextEdit *>("typeTextEditor");
        QTextCursor check(text->document());
        check.setPosition(8);
        QCOMPARE(check.charFormat().fontPointSize(), 28.0);
        QCOMPARE(check.charFormat().fontLetterSpacing(), 3.0);
        check.setPosition(2);
        QCOMPARE(check.charFormat().fontPointSize(), 36.0);
    }
    void liveCanvasCancelRestoresAndAcceptIsOneUndo() {
        auto document = makeDocument(richLayer());
        MainWindow window;
        window.openFile(saveFixture(document.get(), "type-live"));
        Document *opened = window.currentDocument();
        QVERIFY(opened);
        const QImage before = opened->composite();
        const QJsonObject original = opened->activeLayer()->parameters;
        bool sawPreview = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<TypeEditorDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            dialog->findChild<QTextEdit *>("typeTextEditor")->setPlainText("Live preview changed");
            sawPreview = opened->composite() != before;
            dialog->reject();
        });
        window.runCommand("Edit Text...");
        QVERIFY(sawPreview);
        QCOMPARE(opened->composite(), before);
        QCOMPARE(opened->activeLayer()->parameters, original);
        QVERIFY(!opened->canUndo());
        QVERIFY(!opened->isModified());
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<TypeEditorDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            dialog->findChild<QTextEdit *>("typeTextEditor")->setPlainText("Committed typography");
            dialog->accept();
        });
        window.runCommand("Edit Text...");
        QCOMPARE(opened->historyNames().size(), 1);
        QCOMPARE(opened->activeLayer()->text, QString("Committed typography"));
        opened->undo();
        QCOMPARE(opened->composite(), before);
        QVERIFY(!opened->isModified());
        opened->redo();
        QCOMPARE(opened->activeLayer()->text, QString("Committed typography"));
        opened->undo();
    }
    void lockedAncestorRejectsTypographyEditor() {
        auto document = makeDocument(richLayer());
        Layer group;
        group.id = 9;
        group.kind = LayerKind::Group;
        group.locked = true;
        document->state.layers[0].parentId = 9;
        document->state.layers.append(group);
        document->touch();
        document->markSaved();
        MainWindow window;
        window.openFile(saveFixture(document.get(), "locked-type"));
        bool opened = false;
        QTimer::singleShot(0, &window, [&] {
            if (auto *dialog = qobject_cast<TypeEditorDialog *>(QApplication::activeModalWidget())) {
                opened = true;
                dialog->reject();
            }
        });
        window.runCommand("Edit Text...");
        QApplication::processEvents();
        QVERIFY(!opened);
        QVERIFY(!window.currentDocument()->canUndo());
    }
    void mixedColorOutlineConversionCreatesEditableShapesAndUndo() {
        auto document = makeDocument(richLayer());
        MainWindow window;
        window.openFile(saveFixture(document.get(), "type-outlines"));
        Document *opened = window.currentDocument();
        const Layer original = *opened->activeLayer();
        window.runCommand("Convert to Shape");
        QCOMPARE(opened->activeLayer()->kind, LayerKind::Group);
        int shapes = 0;
        for (const Layer &layer : opened->state.layers)
            if (layer.kind == LayerKind::Shape) {
                ++shapes;
                QVERIFY(!layer.shape.isEmpty());
                QCOMPARE(layer.parentId, original.id);
            }
        QVERIFY(shapes >= 2);
        QCOMPARE(opened->historyNames().size(), 1);
        opened->undo();
        QCOMPARE(opened->activeLayer()->kind, LayerKind::Text);
        QCOMPARE(opened->activeLayer()->parameters, original.parameters);
        QVERIFY(!opened->isModified());
    }
};
QTEST_MAIN(TypographyTests)
#include "typography_tests.moc"
