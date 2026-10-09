#include "TypeEditorDialog.h"
#include "document/TextLayout.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

namespace serika {
namespace {
class TextPreview final : public QWidget {
  public:
    Layer layer;
    QSize canvas;
    explicit TextPreview(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("typePreview");
        setMinimumSize(260, 180);
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#262832"));
        const QRectF bounds = textLayerBounds(layer, canvas).adjusted(-12, -12, 12, 12);
        if (bounds.isEmpty())
            return;
        const qreal scale = std::min((width() - 20) / bounds.width(), (height() - 20) / bounds.height());
        painter.translate((width() - bounds.width() * scale) / 2, (height() - bounds.height() * scale) / 2);
        painter.scale(scale, scale);
        painter.translate(-bounds.topLeft());
        painter.fillRect(bounds, QColor("#eeeeef"));
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        paintTextLayer(painter, layer, canvas);
    }
};
QDoubleSpinBox *spin(QWidget *parent, const char *name, double minimum, double maximum, double value = 0) {
    auto *result = new QDoubleSpinBox(parent);
    result->setObjectName(QLatin1String(name));
    result->setRange(minimum, maximum);
    result->setDecimals(2);
    result->setValue(value);
    result->setKeyboardTracking(false);
    return result;
}
Qt::Alignment alignmentAt(int index) {
    return index == 1   ? Qt::AlignHCenter
           : index == 2 ? Qt::AlignRight
           : index == 3 ? Qt::AlignJustify
                        : Qt::AlignLeft;
}
} // namespace
TypeEditorDialog::TypeEditorDialog(const Layer &layer, QSize canvas, QWidget *parent)
    : QDialog(parent), m_original(layer), m_layer(layer), m_canvas(canvas) {
    setObjectName("typeEditorDialog");
    setWindowTitle(tr("Typography"));
    resize(1000, 720);
    auto *root = new QVBoxLayout(this);
    auto *help = new QLabel(tr("Select characters to format a range. Paragraph controls affect the selected "
                               "paragraphs. Changes preview on the canvas; Cancel restores the layer."));
    help->setWordWrap(true);
    root->addWidget(help);
    auto *splitter = new QSplitter;
    root->addWidget(splitter, 1);
    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    m_editor = new QTextEdit;
    m_editor->setObjectName("typeTextEditor");
    m_editor->setAcceptRichText(false);
    populateTextDocument(*m_editor->document(), layer, canvas);
    m_editor->document()->setUndoRedoEnabled(true);
    leftLayout->addWidget(m_editor, 2);
    m_preview = new TextPreview;
    leftLayout->addWidget(m_preview, 1);
    splitter->addWidget(left);
    auto *controls = new QWidget;
    auto *controlLayout = new QVBoxLayout(controls);
    auto *characters = new QGroupBox(tr("Characters"));
    auto *charForm = new QFormLayout(characters);
    m_family = new QFontComboBox;
    m_family->setObjectName("typeFontFamily");
    charForm->addRow(tr("Font"), m_family);
    m_size = spin(characters, "typeFontSize", .1, 4096, 12);
    charForm->addRow(tr("Size (pt)"), m_size);
    auto *style = new QWidget;
    auto *styleLayout = new QHBoxLayout(style);
    styleLayout->setContentsMargins(0, 0, 0, 0);
    m_bold = new QCheckBox(tr("Bold"));
    m_bold->setObjectName("typeBold");
    m_italic = new QCheckBox(tr("Italic"));
    m_italic->setObjectName("typeItalic");
    m_underline = new QCheckBox(tr("Underline"));
    m_underline->setObjectName("typeUnderline");
    styleLayout->addWidget(m_bold);
    styleLayout->addWidget(m_italic);
    styleLayout->addWidget(m_underline);
    charForm->addRow(style);
    auto *color = new QPushButton(tr("Choose text color…"));
    color->setObjectName("typeColor");
    charForm->addRow(color);
    m_tracking = spin(characters, "typeTracking", -1000, 1000);
    m_baseline = spin(characters, "typeBaseline", -1000, 1000);
    m_kerning = new QCheckBox(tr("Font kerning"));
    m_kerning->setObjectName("typeKerning");
    charForm->addRow(tr("Tracking (px)"), m_tracking);
    charForm->addRow(tr("Baseline shift (%)"), m_baseline);
    charForm->addRow(m_kerning);
    auto *vertical = new QComboBox;
    vertical->setObjectName("typeScript");
    vertical->addItems({tr("Normal"), tr("Superscript"), tr("Subscript")});
    charForm->addRow(tr("Position"), vertical);
    auto *caps = new QComboBox;
    caps->setObjectName("typeCapitalization");
    caps->addItems({tr("Mixed case"), tr("Uppercase"), tr("Lowercase"), tr("Small caps")});
    charForm->addRow(tr("Capitalization"), caps);
    auto *stretch = spin(characters, "typeStretch", 1, 400, 100);
    charForm->addRow(tr("Horizontal scale (%)"), stretch);
    controlLayout->addWidget(characters);
    auto *paragraphs = new QGroupBox(tr("Paragraphs"));
    auto *paraForm = new QFormLayout(paragraphs);
    m_alignment = new QComboBox;
    m_alignment->setObjectName("typeAlignment");
    m_alignment->addItems({tr("Left"), tr("Center"), tr("Right"), tr("Justify")});
    paraForm->addRow(tr("Alignment"), m_alignment);
    m_leading = spin(paragraphs, "typeLeading", 0, 10000);
    m_leading->setSpecialValueText(tr("Automatic"));
    m_before = spin(paragraphs, "typeSpaceBefore", 0, 10000);
    m_after = spin(paragraphs, "typeSpaceAfter", 0, 10000);
    m_left = spin(paragraphs, "typeLeftIndent", -10000, 10000);
    m_right = spin(paragraphs, "typeRightIndent", -10000, 10000);
    m_first = spin(paragraphs, "typeFirstIndent", -10000, 10000);
    paraForm->addRow(tr("Leading (px)"), m_leading);
    paraForm->addRow(tr("Before / after (px)"), m_before);
    paraForm->addRow(m_after);
    paraForm->addRow(tr("Left / right indent (px)"), m_left);
    paraForm->addRow(m_right);
    paraForm->addRow(tr("First-line indent (px)"), m_first);
    m_layout = new QComboBox;
    m_layout->setObjectName("typeLayout");
    m_layout->addItems({tr("Paragraph box"), tr("Point text")});
    m_width = spin(paragraphs, "typeBoxWidth", 1, 1000000, canvas.width());
    const auto typography = layer.parameters.value("typography").toObject();
    m_layout->setCurrentIndex(typography.value("layout").toString() == "point" ? 1 : 0);
    m_width->setValue(typography.value("width").toDouble(std::max(1, canvas.width())));
    m_width->setEnabled(m_layout->currentIndex() == 0);
    paraForm->addRow(tr("Layout"), m_layout);
    paraForm->addRow(tr("Box width (px)"), m_width);
    controlLayout->addWidget(paragraphs);
    auto *pathGroup = new QGroupBox(tr("Text on path"));
    auto *pathForm = new QFormLayout(pathGroup);
    m_pathLabel = new QLabel;
    m_pathLabel->setObjectName("typePathStatus");
    pathForm->addRow(m_pathLabel);
    m_pathOffset =
        spin(pathGroup, "typePathOffset", -1000000, 1000000, typography.value("pathOffset").toDouble());
    m_reverse = new QCheckBox(tr("Reverse path direction"));
    m_reverse->setObjectName("typePathReverse");
    m_reverse->setChecked(typography.value("pathReverse").toBool());
    pathForm->addRow(tr("Start distance (px)"), m_pathOffset);
    pathForm->addRow(m_reverse);
    auto *clearPath = new QPushButton(tr("Remove path"));
    clearPath->setObjectName("typeClearPath");
    pathForm->addRow(clearPath);
    controlLayout->addWidget(pathGroup);
    controlLayout->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setWidget(controls);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(325);
    splitter->addWidget(scroll);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->setObjectName("typeEditorButtons");
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_editor, &QTextEdit::textChanged, this, &TypeEditorDialog::changed);
    connect(m_editor, &QTextEdit::cursorPositionChanged, this, &TypeEditorDialog::syncControls);
    connect(m_editor, &QTextEdit::selectionChanged, this, &TypeEditorDialog::syncControls);
    connect(m_family, &QFontComboBox::currentFontChanged, this, [this](const QFont &f) {
        QTextCharFormat format;
        format.setFontFamilies({f.family()});
        applyCharacterFormat(format);
    });
    connect(m_size, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        QTextCharFormat f;
        f.setFontPointSize(v);
        applyCharacterFormat(f);
    });
    connect(m_bold, &QCheckBox::toggled, this, [this](bool v) {
        QTextCharFormat f;
        f.setFontWeight(v ? QFont::Bold : QFont::Normal);
        applyCharacterFormat(f);
    });
    connect(m_italic, &QCheckBox::toggled, this, [this](bool v) {
        QTextCharFormat f;
        f.setFontItalic(v);
        applyCharacterFormat(f);
    });
    connect(m_underline, &QCheckBox::toggled, this, [this](bool v) {
        QTextCharFormat f;
        f.setFontUnderline(v);
        applyCharacterFormat(f);
    });
    connect(m_tracking, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        QTextCharFormat f;
        f.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        f.setFontLetterSpacing(v);
        applyCharacterFormat(f);
    });
    connect(m_baseline, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        QTextCharFormat f;
        f.setBaselineOffset(v);
        applyCharacterFormat(f);
    });
    connect(m_kerning, &QCheckBox::toggled, this, [this](bool v) {
        QTextCharFormat f;
        f.setFontKerning(v);
        applyCharacterFormat(f);
    });
    connect(vertical, &QComboBox::currentIndexChanged, this, [this](int v) {
        QTextCharFormat f;
        f.setVerticalAlignment(v == 1   ? QTextCharFormat::AlignSuperScript
                               : v == 2 ? QTextCharFormat::AlignSubScript
                                        : QTextCharFormat::AlignNormal);
        applyCharacterFormat(f);
    });
    connect(caps, &QComboBox::currentIndexChanged, this, [this](int v) {
        QTextCharFormat f;
        f.setFontCapitalization(v == 1   ? QFont::AllUppercase
                                : v == 2 ? QFont::AllLowercase
                                : v == 3 ? QFont::SmallCaps
                                         : QFont::MixedCase);
        applyCharacterFormat(f);
    });
    connect(stretch, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        QTextCharFormat f;
        f.setFontStretch(qRound(v));
        applyCharacterFormat(f);
    });
    connect(color, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_editor->textCursor().charFormat().foreground().color(),
                                                this, tr("Text color"), QColorDialog::ShowAlphaChannel);
        if (c.isValid()) {
            QTextCharFormat f;
            f.setForeground(c);
            applyCharacterFormat(f);
        }
    });
    connect(m_alignment, &QComboBox::currentIndexChanged, this, [this] { applyParagraphFormat(); });
    for (auto *field : {m_leading, m_before, m_after, m_left, m_right, m_first})
        connect(field, &QDoubleSpinBox::valueChanged, this, [this] { applyParagraphFormat(); });
    connect(m_layout, &QComboBox::currentIndexChanged, this, [this] {
        if (m_syncing)
            return;
        m_width->setEnabled(m_layout->currentIndex() == 0);
        changed();
    });
    connect(m_width, &QDoubleSpinBox::valueChanged, this, [this] { changed(); });
    connect(m_pathOffset, &QDoubleSpinBox::valueChanged, this, [this] { changed(); });
    connect(m_reverse, &QCheckBox::toggled, this, [this] { changed(); });
    connect(clearPath, &QPushButton::clicked, this, [this] { setTextPath({}); });
    syncControls();
    updatePreview();
}
Layer TypeEditorDialog::editedLayer() const { return m_dirty ? m_layer : m_original; }
void TypeEditorDialog::applyCharacterFormat(const QTextCharFormat &format) {
    if (m_syncing)
        return;
    QTextCursor cursor = m_editor->textCursor();
    // Without a selection, format the current word; an empty word sets the insertion format.
    if (!cursor.hasSelection())
        cursor.select(QTextCursor::WordUnderCursor);
    cursor.mergeCharFormat(format);
    m_editor->mergeCurrentCharFormat(format);
    changed();
}
void TypeEditorDialog::applyParagraphFormat() {
    if (m_syncing)
        return;
    QTextBlockFormat format;
    format.setAlignment(alignmentAt(m_alignment->currentIndex()));
    format.setTopMargin(m_before->value());
    format.setBottomMargin(m_after->value());
    format.setLeftMargin(m_left->value());
    format.setRightMargin(m_right->value());
    format.setTextIndent(m_first->value());
    format.setLineHeight(m_leading->value(), m_leading->value() > 0 ? QTextBlockFormat::FixedHeight
                                                                    : QTextBlockFormat::SingleHeight);
    QTextCursor cursor = m_editor->textCursor();
    cursor.mergeBlockFormat(format);
    changed();
}
void TypeEditorDialog::syncControls() {
    m_syncing = true;
    const auto character = m_editor->textCursor().charFormat();
    const QFont font = character.font().resolve(m_layer.font);
    m_family->setCurrentFont(font);
    m_size->setValue(font.pointSizeF() > 0 ? font.pointSizeF() : font.pixelSize() * 72.0 / 96.0);
    m_bold->setChecked(font.bold());
    m_italic->setChecked(font.italic());
    m_underline->setChecked(font.underline());
    m_kerning->setChecked(font.kerning());
    m_tracking->setValue(font.letterSpacingType() == QFont::AbsoluteSpacing ? font.letterSpacing() : 0);
    m_baseline->setValue(character.baselineOffset());
    findChild<QComboBox *>("typeScript")
        ->setCurrentIndex(character.verticalAlignment() == QTextCharFormat::AlignSuperScript ? 1
                          : character.verticalAlignment() == QTextCharFormat::AlignSubScript ? 2
                                                                                             : 0);
    findChild<QComboBox *>("typeCapitalization")
        ->setCurrentIndex(font.capitalization() == QFont::AllUppercase   ? 1
                          : font.capitalization() == QFont::AllLowercase ? 2
                          : font.capitalization() == QFont::SmallCaps    ? 3
                                                                         : 0);
    findChild<QDoubleSpinBox *>("typeStretch")
        ->setValue(font.stretch() == QFont::AnyStretch ? QFont::Unstretched : font.stretch());
    const auto paragraph = m_editor->textCursor().blockFormat();
    m_alignment->setCurrentIndex(paragraph.alignment().testFlag(Qt::AlignJustify)   ? 3
                                 : paragraph.alignment().testFlag(Qt::AlignHCenter) ? 1
                                 : paragraph.alignment().testFlag(Qt::AlignRight)   ? 2
                                                                                    : 0);
    m_leading->setValue(paragraph.lineHeightType() == QTextBlockFormat::FixedHeight ? paragraph.lineHeight()
                                                                                    : 0);
    m_before->setValue(paragraph.topMargin());
    m_after->setValue(paragraph.bottomMargin());
    m_left->setValue(paragraph.leftMargin());
    m_right->setValue(paragraph.rightMargin());
    m_first->setValue(paragraph.textIndent());
    m_syncing = false;
}
void TypeEditorDialog::setTextPath(const QPainterPath &path) {
    auto typography = m_layer.parameters.value("typography").toObject();
    typography["path"] = textPathToJson(path);
    m_layer.parameters["typography"] = typography;
    changed();
}
void TypeEditorDialog::changed() {
    if (m_syncing)
        return;
    m_dirty = true;
    serializeTextDocument(m_layer, *m_editor->document());
    auto typography = m_layer.parameters.value("typography").toObject();
    typography["layout"] = m_layout->currentIndex() == 1 ? "point" : "paragraph";
    typography["width"] = m_width->value();
    typography["pathOffset"] = m_pathOffset->value();
    typography["pathReverse"] = m_reverse->isChecked();
    m_layer.parameters["typography"] = typography;
    m_editor->document()->setTextWidth(m_layout->currentIndex() == 1 ? -1 : m_width->value());
    updatePreview();
    emit previewChanged();
}
void TypeEditorDialog::updatePreview() {
    auto *preview = static_cast<TextPreview *>(m_preview);
    preview->layer = editedLayer();
    preview->canvas = m_canvas;
    preview->update();
    const bool onPath = !m_layer.parameters.value("typography").toObject().value("path").toArray().isEmpty();
    m_pathLabel->setText(onPath ? tr("Following an editable path; line breaks become spaces.")
                                : tr("No path. Use Type on Path to choose a shape layer."));
    m_pathOffset->setEnabled(onPath);
    m_reverse->setEnabled(onPath);
}
} // namespace serika
