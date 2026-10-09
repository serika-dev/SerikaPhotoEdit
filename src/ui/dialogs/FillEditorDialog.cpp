#include "FillEditorDialog.h"
#include "document/FillRenderer.h"
#include <QBuffer>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
namespace serika {
FillEditorDialog::FillEditorDialog(LayerKind kind, const QJsonObject &params, QColor foreground,
                                   QColor background, QWidget *parent, bool gradientMap)
    : QDialog(parent), m_kind(kind), m_gradientMap(gradientMap), m_parameters(params) {
    setWindowTitle(kind == LayerKind::PatternFill ? "Pattern Fill" : "Gradient Fill");
    resize(550, kind == LayerKind::PatternFill ? 360 : 620);
    auto *layout = new QVBoxLayout(this);
    m_preview = new QLabel;
    m_preview->setObjectName("fillPreview");
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(160);
    layout->addWidget(m_preview);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    m_angle = new QDoubleSpinBox(this);
    m_angle->setObjectName("fillAngle");
    m_angle->setRange(-360, 360);
    m_angle->setSuffix("°");
    m_angle->setValue(params.value("angle").toDouble());
    m_scale = new QDoubleSpinBox(this);
    m_scale->setObjectName("fillScale");
    m_scale->setRange(1, 1000);
    m_scale->setSuffix(" %");
    m_scale->setValue(params.value("scale").toDouble(100));
    if (kind == LayerKind::GradientFill) {
        m_style = new QComboBox(this);
        m_style->setObjectName("gradientStyle");
        m_style->addItems({"Linear", "Radial", "Angle", "Reflected", "Diamond"});
        m_style->setCurrentText(params.value("style").toString("Linear"));
        if (!m_gradientMap)
            form->addRow("Style", m_style);
        else
            m_style->hide();
        m_reverse = new QCheckBox("Reverse colors");
        m_reverse->setChecked(params.value("reverse").toBool());
        form->addRow(m_reverse);
        auto unreversed = params;
        unreversed["reverse"] = false;
        m_stops = gradientStops(unreversed, foreground, background);
        m_list = new QListWidget;
        m_list->setObjectName("gradientStops");
        m_list->setMaximumHeight(130);
        layout->addWidget(m_list);
        auto *stopFields = new QHBoxLayout;
        m_position = new QDoubleSpinBox;
        m_position->setObjectName("gradientStopPosition");
        m_position->setRange(0, 100);
        m_position->setSuffix(" %");
        m_opacity = new QDoubleSpinBox;
        m_opacity->setObjectName("gradientStopOpacity");
        m_opacity->setRange(0, 100);
        m_opacity->setSuffix(" %");
        auto *color = new QPushButton("Color…");
        stopFields->addWidget(new QLabel("Position"));
        stopFields->addWidget(m_position);
        stopFields->addWidget(new QLabel("Opacity"));
        stopFields->addWidget(m_opacity);
        stopFields->addWidget(color);
        layout->addLayout(stopFields);
        auto *row = new QHBoxLayout;
        auto *add = new QPushButton("Add stop");
        add->setObjectName("gradientAddStop");
        auto *remove = new QPushButton("Remove stop");
        remove->setObjectName("gradientRemoveStop");
        row->addWidget(add);
        row->addWidget(remove);
        row->addStretch();
        layout->addLayout(row);
        connect(m_list, &QListWidget::currentRowChanged, this, [this](int index) {
            if (index < 0 || index >= m_stops.size())
                return;
            QSignalBlocker p(m_position), a(m_opacity);
            m_position->setValue(m_stops[index].first * 100);
            m_opacity->setValue(m_stops[index].second.alphaF() * 100);
        });
        connect(m_position, &QDoubleSpinBox::valueChanged, this, [this](double value) {
            int i = m_list->currentRow();
            if (i < 0)
                return;
            m_stops[i].first = value / 100;
            refreshStops();
        });
        connect(m_opacity, &QDoubleSpinBox::valueChanged, this, [this](double value) {
            int i = m_list->currentRow();
            if (i < 0)
                return;
            m_stops[i].second.setAlphaF(value / 100);
            refreshStops();
        });
        connect(color, &QPushButton::clicked, this, [this] {
            int i = m_list->currentRow();
            if (i < 0)
                return;
            const auto c =
                QColorDialog::getColor(m_stops[i].second, this, "Stop color", QColorDialog::ShowAlphaChannel);
            if (c.isValid()) {
                m_stops[i].second = c;
                refreshStops();
            }
        });
        connect(add, &QPushButton::clicked, this, [this] {
            if (m_stops.size() >= 64)
                return;
            auto p = parameters();
            p["reverse"] = false;
            const auto sorted = gradientStops(p);
            qreal gap = 0, position = .5;
            for (int i = 1; i < sorted.size(); ++i)
                if (sorted[i].first - sorted[i - 1].first > gap) {
                    gap = sorted[i].first - sorted[i - 1].first;
                    position = (sorted[i].first + sorted[i - 1].first) / 2;
                }
            m_stops.append({position, sampleGradient(sorted, position)});
            refreshStops();
            m_list->setCurrentRow(int(m_stops.size()) - 1);
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            if (m_stops.size() <= 2)
                return;
            int i = m_list->currentRow();
            if (i < 0)
                return;
            m_stops.remove(i);
            refreshStops();
        });
        connect(m_style, &QComboBox::currentIndexChanged, this, [this] { refreshPreview(); });
        connect(m_reverse, &QCheckBox::toggled, this, [this] { refreshPreview(); });
        refreshStops();
    } else {
        auto *load = new QPushButton("Load pattern image…");
        form->addRow(load);
        auto *hint =
            new QLabel("Tiles repeat across the canvas. Loaded patterns are embedded (up to 512 × 512 px).");
        hint->setWordWrap(true);
        layout->addWidget(hint);
        connect(load, &QPushButton::clicked, this, [this] {
            const auto path = QFileDialog::getOpenFileName(
                this, "Load pattern", {}, "Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)");
            if (path.isEmpty())
                return;
            QImageReader reader(path);
            const QSize size = reader.size();
            if (size.isValid() && (size.width() > 512 || size.height() > 512))
                reader.setScaledSize(size.scaled(512, 512, Qt::KeepAspectRatio));
            QImage image = reader.read();
            if (image.isNull()) {
                QMessageBox::warning(this, "Load pattern", reader.errorString());
                return;
            }
            if (image.width() > 512 || image.height() > 512)
                image = image.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QByteArray data;
            QBuffer buffer(&data);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            m_parameters["patternPng"] = QString::fromLatin1(data.toBase64());
            refreshPreview();
        });
    }
    if (!m_gradientMap) {
        form->addRow("Angle", m_angle);
        form->addRow("Scale", m_scale);
    } else {
        m_angle->hide();
        m_scale->hide();
    }
    connect(m_angle, &QDoubleSpinBox::valueChanged, this, [this] { refreshPreview(); });
    connect(m_scale, &QDoubleSpinBox::valueChanged, this, [this] { refreshPreview(); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    refreshPreview();
}
QJsonObject FillEditorDialog::parameters() const {
    auto result = m_parameters;
    result["angle"] = m_gradientMap ? 0 : m_angle->value();
    result["scale"] = m_gradientMap ? 100 : m_scale->value();
    if (m_kind == LayerKind::GradientFill) {
        QJsonArray stops;
        for (const auto &stop : m_stops)
            stops.append(QJsonObject{{"position", stop.first}, {"color", stop.second.name(QColor::HexArgb)}});
        result["stops"] = stops;
        result["style"] = m_gradientMap ? QString("Linear") : m_style->currentText();
        result["reverse"] = m_reverse->isChecked();
    }
    return result;
}
void FillEditorDialog::refreshPreview() {
    const auto p = parameters();
    QImage image = m_kind == LayerKind::GradientFill ? renderGradientFill({480, 160}, 8, p)
                                                     : renderPatternFill({480, 160}, 8, p);
    QImage preview(image.size(), QImage::Format_RGB32);
    QPainter painter(&preview);
    for (int y = 0; y < preview.height(); y += 12)
        for (int x = 0; x < preview.width(); x += 12)
            painter.fillRect(x, y, 12, 12,
                             (x / 12 + y / 12) % 2 ? QColor(180, 180, 180) : QColor(225, 225, 225));
    painter.drawImage(0, 0, image);
    painter.end();
    m_preview->setPixmap(QPixmap::fromImage(preview));
}
void FillEditorDialog::refreshStops() {
    const int selected = std::clamp(m_list->currentRow(), 0, int(m_stops.size()) - 1);
    {
        QSignalBlocker blocker(m_list);
        m_list->clear();
        for (const auto &stop : m_stops) {
            auto *item = new QListWidgetItem(QString("%1%   %2   ·   %3% opacity")
                                                 .arg(stop.first * 100, 0, 'f', 1)
                                                 .arg(stop.second.name())
                                                 .arg(stop.second.alphaF() * 100, 0, 'f', 0),
                                             m_list);
            QPixmap swatch(20, 20);
            swatch.fill(stop.second);
            item->setIcon(QIcon(swatch));
        }
    }
    m_list->setCurrentRow(selected);
    refreshPreview();
}
} // namespace serika
