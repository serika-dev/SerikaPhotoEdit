#include "MaskRefineDialog.h"
#include "document/Document.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <functional>
namespace serika {
class MaskRefineDialog::Preview : public QWidget {
  public:
    QImage source, mask;
    QString view = "Overlay";
    qreal opacity = .5;
    int radius = 25;
    std::function<void(QPointF, bool, int)> paintMask;
    Preview(QWidget *parent) : QWidget(parent) {
        setMinimumSize(480, 400);
        setMouseTracking(true);
        setObjectName("maskPreview");
    }
    QRectF imageRect() const {
        QSizeF fit = source.size();
        fit.scale(size() - QSize(24, 24), Qt::KeepAspectRatio);
        return QRectF(QPointF((width() - fit.width()) / 2, (height() - fit.height()) / 2), fit);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#242424"));
        if (source.isNull())
            return;
        const QRectF target = imageRect();
        QImage display =
            source.scaled(target.size().toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                .convertToFormat(QImage::Format_RGBA8888);
        const QImage small = mask.scaled(display.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        for (int y = 0; y < display.height(); ++y)
            for (int x = 0; x < display.width(); ++x) {
                const qreal a = maskSample(small, x, y);
                QColor c = display.pixelColor(x, y), bg;
                if (view == "Black & white") {
                    display.setPixelColor(x, y, QColor::fromRgbF(a, a, a));
                    continue;
                }
                qreal blend = 1 - a;
                if (view == "Overlay") {
                    bg = QColor(235, 45, 70);
                    blend *= opacity;
                } else if (view == "On black")
                    bg = Qt::black;
                else if (view == "On white")
                    bg = Qt::white;
                else {
                    bg = ((x / 12 + y / 12) % 2) ? QColor(150, 150, 150) : QColor(210, 210, 210);
                    if (view == "Onion skin")
                        blend *= opacity;
                }
                display.setPixelColor(x, y,
                                      QColor::fromRgbF(c.redF() * (1 - blend) + bg.redF() * blend,
                                                       c.greenF() * (1 - blend) + bg.greenF() * blend,
                                                       c.blueF() * (1 - blend) + bg.blueF() * blend));
            }
        p.drawImage(target, display);
        p.setPen(QPen(Qt::white, 1));
        p.drawRect(target);
    }
    void apply(QMouseEvent *e) {
        if (!paintMask || !imageRect().contains(e->position()))
            return;
        const QRectF r = imageRect();
        const QPointF pixel((e->position().x() - r.left()) * source.width() / r.width(),
                            (e->position().y() - r.top()) * source.height() / r.height());
        paintMask(pixel, !(e->buttons() & Qt::RightButton) && !(e->modifiers() & Qt::AltModifier), radius);
    }
    void mousePressEvent(QMouseEvent *e) override { apply(e); }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (e->buttons() & (Qt::LeftButton | Qt::RightButton))
            apply(e);
    }
};
MaskRefineDialog::MaskRefineDialog(const QImage &source, const QImage &mask, int depth, QWidget *parent)
    : QDialog(parent), m_source(source), m_original(normalizeMask(mask, depth)), m_depth(depth) {
    setWindowTitle("Select and Mask");
    setObjectName("selectAndMaskDialog");
    resize(1060, 720);
    auto *outer = new QVBoxLayout(this);
    auto *body = new QHBoxLayout;
    outer->addLayout(body, 1);
    m_preview = new Preview(this);
    m_preview->source = source;
    body->addWidget(m_preview, 1);
    auto *controls = new QWidget;
    controls->setFixedWidth(245);
    auto *form = new QFormLayout(controls);
    body->addWidget(controls);
    auto *view = new QComboBox;
    view->setObjectName("maskView");
    view->addItems({"Overlay", "On black", "On white", "Black & white", "On layers", "Onion skin"});
    form->addRow("View", view);
    auto *opacity = new QSpinBox;
    opacity->setObjectName("viewOpacity");
    opacity->setRange(0, 100);
    opacity->setValue(50);
    opacity->setSuffix("%");
    form->addRow("View opacity", opacity);
    auto add = [&](const QString &label, const QString &key, double min, double max, double initial,
                   double step = 1.) {
        auto *s = new QDoubleSpinBox;
        s->setObjectName(key);
        s->setRange(min, max);
        s->setSingleStep(step);
        s->setDecimals(step < 1 ? 1 : 0);
        s->setValue(initial);
        form->addRow(label, s);
        return s;
    };
    add("Edge radius (px)", "radius", 0, 100, 0, .5);
    add("Smooth", "smooth", 0, 100, 0);
    add("Feather (px)", "feather", 0, 100, 0, .5);
    add("Contrast (%)", "contrast", 0, 100, 0);
    add("Shift edge (px)", "shiftEdge", -100, 100, 0, .5);
    auto *smart = new QCheckBox("Smart radius");
    smart->setObjectName("smartRadius");
    form->addRow(smart);
    auto *brush = new QSpinBox;
    brush->setObjectName("refineBrushSize");
    brush->setRange(1, 500);
    brush->setValue(25);
    form->addRow("Refine brush (px)", brush);
    auto *hint = new QLabel("Paint to add. Alt or right-click to subtract.");
    hint->setWordWrap(true);
    form->addRow(hint);
    auto *decontam = new QCheckBox("Decontaminate colors");
    decontam->setObjectName("decontaminate");
    form->addRow(decontam);
    add("Decontaminate (%)", "decontaminateAmount", 0, 100, 50);
    auto *output = new QComboBox;
    output->setObjectName("maskOutput");
    output->addItems({"Selection", "Layer mask", "New layer", "New layer with mask"});
    form->addRow("Output to", output);
    auto *reset = new QPushButton("Reset refinement");
    form->addRow(reset);
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(90);
    connect(timer, &QTimer::timeout, this, &MaskRefineDialog::updatePreview);
    for (auto *s : findChildren<QDoubleSpinBox *>())
        connect(s, &QDoubleSpinBox::valueChanged, this, [timer] { timer->start(); });
    connect(view, &QComboBox::currentTextChanged, this, [this] { updatePreview(); });
    connect(opacity, &QSpinBox::valueChanged, this, [this] { updatePreview(); });
    connect(smart, &QCheckBox::toggled, this, [timer] { timer->start(); });
    connect(brush, &QSpinBox::valueChanged, this, [this](int n) { m_preview->radius = n; });
    connect(decontam, &QCheckBox::toggled, this, [output](bool on) {
        if (on && output->currentIndex() < 2)
            output->setCurrentIndex(3);
    });
    connect(reset, &QPushButton::clicked, this, [this, mask, depth] {
        m_original = normalizeMask(mask, depth);
        for (auto *s : findChildren<QDoubleSpinBox *>())
            if (s->objectName() != "decontaminateAmount")
                s->setValue(0);
        updatePreview();
    });
    m_preview->paintMask = [this, timer](QPointF center, bool add, int radius) {
        const QRect bounds =
            QRect(qFloor(center.x() - radius), qFloor(center.y() - radius), 2 * radius + 1, 2 * radius + 1)
                .intersected(m_original.rect());
        for (int y = bounds.top(); y <= bounds.bottom(); ++y)
            for (int x = bounds.left(); x <= bounds.right(); ++x) {
                const double distance = std::hypot(x - center.x(), y - center.y());
                if (distance > radius)
                    continue;
                const double amount = std::clamp((radius - distance) / std::max(1., radius * .2), 0., 1.);
                const double old = maskSample(m_original, x, y);
                setMaskSample(m_original, x, y, add ? old + (1 - old) * amount : old * (1 - amount));
            }
        timer->start();
    };
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        updatePreview();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updatePreview();
}
void MaskRefineDialog::updatePreview() {
    QJsonObject params;
    for (auto *s : findChildren<QDoubleSpinBox *>())
        params[s->objectName()] = s->value();
    params["smartRadius"] = findChild<QCheckBox *>("smartRadius")->isChecked();
    m_refined = refineMask(m_original, params, m_source);
    m_preview->mask = m_refined;
    m_preview->view = findChild<QComboBox *>("maskView")->currentText();
    m_preview->opacity = findChild<QSpinBox *>("viewOpacity")->value() / 100.;
    m_preview->update();
}
QImage MaskRefineDialog::refinedMask() const { return m_refined; }
MaskRefineDialog::Output MaskRefineDialog::outputMode() const {
    return Output(findChild<QComboBox *>("maskOutput")->currentIndex());
}
QImage MaskRefineDialog::outputImage() const {
    QImage result = m_source.convertToFormat(QImage::Format_RGBA32FPx4);
    if (findChild<QCheckBox *>("decontaminate")->isChecked()) {
        const double amount = findChild<QDoubleSpinBox *>("decontaminateAmount")->value() / 100.;
        const auto original = result;
        for (int y = 0; y < result.height(); ++y)
            for (int x = 0; x < result.width(); ++x) {
                const double a = maskSample(m_refined, x, y);
                if (a < .01 || a > .99)
                    continue;
                QPoint target;
                bool found = false;
                for (int r = 1; r <= 8 && !found; ++r)
                    for (int dy = -r; dy <= r && !found; ++dy)
                        for (int dx = -r; dx <= r && !found; ++dx)
                            if (maskSample(m_refined, x + dx, y + dy) > .98) {
                                target = QPoint(x + dx, y + dy);
                                found = true;
                            }
                if (found) {
                    const double blend = (1 - a) * amount;
                    auto *pixel = reinterpret_cast<float *>(result.scanLine(y)) + x * 4;
                    const auto *source = reinterpret_cast<const float *>(original.constScanLine(y)) + x * 4;
                    const auto *edge =
                        reinterpret_cast<const float *>(original.constScanLine(target.y())) + target.x() * 4;
                    for (int channel = 0; channel < 3; ++channel)
                        pixel[channel] = float(source[channel] * (1 - blend) + edge[channel] * blend);
                }
            }
    }
    if (outputMode() == Output::NewLayer)
        for (int y = 0; y < result.height(); ++y)
            for (int x = 0; x < result.width(); ++x) {
                auto *pixel = reinterpret_cast<float *>(result.scanLine(y)) + x * 4;
                pixel[3] *= float(maskSample(m_refined, x, y));
            }
    return result.convertToFormat(m_depth == 32   ? QImage::Format_RGBA32FPx4
                                  : m_depth == 16 ? QImage::Format_RGBA64
                                                  : QImage::Format_RGBA8888);
}
} // namespace serika
