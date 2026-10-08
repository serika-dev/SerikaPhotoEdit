#include "LiquifyDialog.h"
#include <QColorSpace>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace serika {
namespace {
class LiquifyCanvas final : public QWidget {
  public:
    QImage source;
    QImage preview;
    QImage frozen;
    QImage frozenBefore;
    QVector<QPointF> mesh;
    QVector<QPointF> before;
    int mode = 0;
    int radius = 60;
    qreal strength = 0.5;
    int columns = 0, rows = 0;
    qreal step = 12;
    QPointF previous, cursor;
    bool dragging = false;
    QImage rendered;
    bool previewDirty = true;
    explicit LiquifyCanvas(const QImage &image, QWidget *parent) : QWidget(parent), source(image) {
        setObjectName("liquifyCanvas");
        setMouseTracking(true);
        setMinimumSize(500, 350);
        const QSize size = image.width() > 1000 || image.height() > 760
                               ? image.size().scaled(1000, 760, Qt::KeepAspectRatio)
                               : image.size();
        preview = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        frozen = QImage(size, QImage::Format_Grayscale8);
        frozen.fill(0);
        columns = int(std::ceil(size.width() / step)) + 1;
        rows = int(std::ceil(size.height() / step)) + 1;
        mesh.resize(columns * rows);
    }
    QRectF displayRect() const {
        const QSizeF size =
            QSizeF(preview.size()).scaled(QSizeF(width() - 32, height() - 32), Qt::KeepAspectRatio);
        return QRectF(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    }
    QPointF toPreview(QPointF point) const {
        const QRectF rect = displayRect();
        return QPointF((point.x() - rect.x()) * preview.width() / rect.width(),
                       (point.y() - rect.y()) * preview.height() / rect.height());
    }
    QPointF displacement(QPointF p) const {
        const qreal gx = std::clamp(p.x() / step, qreal(0), qreal(columns - 1)),
                    gy = std::clamp(p.y() / step, qreal(0), qreal(rows - 1));
        const int x = std::min(int(gx), columns - 2), y = std::min(int(gy), rows - 2);
        const qreal fx = gx - x, fy = gy - y;
        return mesh[y * columns + x] * (1 - fx) * (1 - fy) + mesh[y * columns + x + 1] * fx * (1 - fy) +
               mesh[(y + 1) * columns + x] * (1 - fx) * fy + mesh[(y + 1) * columns + x + 1] * fx * fy;
    }
    QImage render(bool full) const {
        const QImage input = full ? source : preview;
        QImage output(input.size(), input.format());
        output.fill(Qt::transparent);
        output.setColorSpace(input.colorSpace());
        const qreal sx = qreal(input.width()) / preview.width(),
                    sy = qreal(input.height()) / preview.height();
        for (int y = 0; y < input.height(); ++y)
            for (int x = 0; x < input.width(); ++x) {
                const QPointF delta = displacement(QPointF(x / sx, y / sy));
                const qreal px = std::clamp(x + delta.x() * sx, qreal(0), qreal(input.width() - 1)),
                            py = std::clamp(y + delta.y() * sy, qreal(0), qreal(input.height() - 1));
                const int ix = int(px), iy = int(py), nx = std::min(ix + 1, input.width() - 1),
                          ny = std::min(iy + 1, input.height() - 1);
                const qreal fx = px - ix, fy = py - iy;
                const QColor a = input.pixelColor(ix, iy), b = input.pixelColor(nx, iy),
                             c = input.pixelColor(ix, ny), d = input.pixelColor(nx, ny);
                auto interpolate = [&](qreal va, qreal vb, qreal vc, qreal vd) {
                    return va * (1 - fx) * (1 - fy) + vb * fx * (1 - fy) + vc * (1 - fx) * fy + vd * fx * fy;
                };
                output.setPixelColor(
                    x, y,
                    QColor::fromRgbF(interpolate(a.redF(), b.redF(), c.redF(), d.redF()),
                                     interpolate(a.greenF(), b.greenF(), c.greenF(), d.greenF()),
                                     interpolate(a.blueF(), b.blueF(), c.blueF(), d.blueF()),
                                     interpolate(a.alphaF(), b.alphaF(), c.alphaF(), d.alphaF())));
            }
        return output;
    }
    void reset() {
        std::fill(mesh.begin(), mesh.end(), QPointF());
        previewDirty = true;
        update();
    }
    void paintEvent(QPaintEvent *) override {
        if (previewDirty) {
            rendered = render(false);
            previewDirty = false;
        }
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#222222"));
        const QRectF display = displayRect();
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(display, rendered);
        QImage overlay(frozen.size(), QImage::Format_ARGB32);
        overlay.fill(Qt::transparent);
        for (int y = 0; y < frozen.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(overlay.scanLine(y));
            for (int x = 0; x < frozen.width(); ++x)
                row[x] = qRgba(232, 62, 71, frozen.constScanLine(y)[x] / 2);
        }
        painter.drawImage(display, overlay);
        if (underMouse()) {
            painter.setPen(QPen(Qt::white, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(cursor, radius * display.width() / preview.width(),
                                radius * display.height() / preview.height());
        }
    }
    void apply(QPointF point) {
        if (mode == 6 || mode == 7) {
            QPainter painter(&frozen);
            painter.setPen(Qt::NoPen);
            painter.setBrush(mode == 6 ? Qt::white : Qt::black);
            painter.drawEllipse(point, radius, radius);
            update();
            return;
        }
        const QVector<QPointF> old = mesh;
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < columns; ++x) {
                const QPointF position(x * step, y * step);
                const QPointF vector = position - point;
                const qreal length = std::hypot(vector.x(), vector.y());
                if (length >= radius)
                    continue;
                const int px = std::clamp(int(position.x()), 0, frozen.width() - 1),
                          py = std::clamp(int(position.y()), 0, frozen.height() - 1);
                if (frozen.constScanLine(py)[px] > 127)
                    continue;
                const qreal weight = std::pow(1 - length / radius, 2) * strength;
                QPointF &delta = mesh[y * columns + x];
                if (mode == 0)
                    delta -= (point - previous) * weight;
                else if (mode == 1) {
                    const qreal angle = weight * 0.22;
                    const QPointF rotated(vector.x() * std::cos(angle) - vector.y() * std::sin(angle),
                                          vector.x() * std::sin(angle) + vector.y() * std::cos(angle));
                    delta += rotated - vector;
                } else if (mode == 2)
                    delta += vector * weight * 0.16;
                else if (mode == 3)
                    delta -= vector * weight * 0.16;
                else if (mode == 4)
                    delta *= 1 - weight;
                else if (mode == 5) {
                    QPointF average;
                    int count = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            if (x + dx >= 0 && x + dx < columns && y + dy >= 0 && y + dy < rows) {
                                average += old[(y + dy) * columns + x + dx];
                                ++count;
                            }
                    delta = delta * (1 - weight) + average * (weight / count);
                }
            }
        previous = point;
        previewDirty = true;
        update();
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton)
            return;
        cursor = event->position();
        previous = toPreview(cursor);
        dragging = true;
        before = mesh;
        frozenBefore = frozen;
        apply(previous);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        cursor = event->position();
        if (dragging)
            apply(toPreview(cursor));
        else
            update();
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton)
            dragging = false;
    }
};
} // namespace

QImage LiquifyDialog::edit(const QImage &image, QWidget *parent) {
    if (image.isNull())
        return {};
    LiquifyDialog dialog;
    dialog.setParent(parent, dialog.windowFlags());
    dialog.setWindowTitle(QObject::tr("Liquify"));
    dialog.resize(1060, 820);
    auto *layout = new QVBoxLayout(&dialog);
    auto *controls = new QHBoxLayout;
    auto *tools = new QComboBox(&dialog);
    tools->setObjectName("liquifyTools");
    tools->addItems({QObject::tr("Forward Warp"), QObject::tr("Twirl"), QObject::tr("Pucker"),
                     QObject::tr("Bloat"), QObject::tr("Reconstruct"), QObject::tr("Smooth"),
                     QObject::tr("Freeze Mask"), QObject::tr("Thaw Mask")});
    controls->addWidget(tools);
    controls->addWidget(new QLabel(QObject::tr("Brush size"), &dialog));
    auto *size = new QSpinBox(&dialog);
    size->setRange(5, 500);
    size->setValue(120);
    controls->addWidget(size);
    controls->addWidget(new QLabel(QObject::tr("Pressure"), &dialog));
    auto *pressure = new QSlider(Qt::Horizontal, &dialog);
    pressure->setRange(1, 100);
    pressure->setValue(50);
    pressure->setMaximumWidth(150);
    controls->addWidget(pressure);
    auto *undo = new QPushButton(QObject::tr("Undo Stroke"), &dialog);
    controls->addWidget(undo);
    auto *reset = new QPushButton(QObject::tr("Restore All"), &dialog);
    controls->addWidget(reset);
    controls->addStretch();
    layout->addLayout(controls);
    auto *canvas = new LiquifyCanvas(image, &dialog);
    layout->addWidget(canvas, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(tools, &QComboBox::currentIndexChanged, canvas,
                     [canvas](int value) { canvas->mode = value; });
    QObject::connect(size, &QSpinBox::valueChanged, canvas, [canvas](int value) {
        canvas->radius = value / 2;
        canvas->update();
    });
    QObject::connect(pressure, &QSlider::valueChanged, canvas,
                     [canvas](int value) { canvas->strength = value / 100.0; });
    QObject::connect(undo, &QPushButton::clicked, canvas, [canvas] {
        if (!canvas->before.isEmpty()) {
            canvas->mesh = canvas->before;
            if (!canvas->frozenBefore.isNull())
                canvas->frozen = canvas->frozenBefore;
            canvas->previewDirty = true;
            canvas->update();
        }
    });
    QObject::connect(reset, &QPushButton::clicked, canvas, [canvas] { canvas->reset(); });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    return dialog.exec() == QDialog::Accepted ? canvas->render(true) : QImage();
}
} // namespace serika
