#include "TransformDialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtMath>
#include <cmath>
#include <functional>
namespace serika {
class TransformDialog::Preview : public QWidget {
  public:
    QImage image;
    QPolygonF quad;
    QString mode = "Free Transform";
    int handle = -2;
    QPointF previous;
    bool valid = true;
    std::function<void()> changed;
    Preview(const QImage &source, QWidget *parent) : QWidget(parent), image(source) {
        setObjectName("transformPreview");
        setMinimumSize(480, 400);
        reset();
    }
    QPolygonF original() const {
        return {{0, 0},
                {qreal(image.width()), 0},
                {qreal(image.width()), qreal(image.height())},
                {0, qreal(image.height())}};
    }
    void reset() {
        quad = original();
        update();
    }
    QTransform matrix() const {
        QTransform t;
        return QTransform::quadToQuad(original(), quad, t) ? t : QTransform();
    }
    qreal zoom() const {
        return qMin((width() - 120.) / qMax(1, image.width()), (height() - 120.) / qMax(1, image.height())) *
               .75;
    }
    QPointF origin() const {
        return QPointF(width() / 2., height() / 2.) -
               QPointF(image.width() / 2., image.height() / 2.) * zoom();
    }
    QPointF toImage(QPointF p) const { return (p - origin()) / zoom(); }
    QPointF toView(QPointF p) const { return origin() + p * zoom(); }
    QPointF rotationKnob() const {
        const auto center = quad.boundingRect().center();
        const auto top = (quad[0] + quad[1]) / 2;
        auto v = top - center;
        const double length = std::hypot(v.x(), v.y());
        return length > .001 ? top + v / length * (26 / zoom()) : top;
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#242424"));
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        for (int y = 0; y < height(); y += 16)
            for (int x = 0; x < width(); x += 16)
                if ((x / 16 + y / 16) % 2)
                    p.fillRect(x, y, 16, 16, QColor("#2e2e2e"));
        p.save();
        p.translate(origin());
        p.scale(zoom(), zoom());
        p.setTransform(matrix(), true);
        p.drawImage(0, 0, image);
        p.restore();
        QPolygonF screen;
        for (auto q : quad)
            screen << toView(q);
        p.setPen(QPen(QColor("#e8893a"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(screen);
        for (auto q : screen) {
            p.setBrush(Qt::white);
            p.setPen(QColor("#333"));
            p.drawRect(QRectF(q - QPointF(4, 4), QSizeF(8, 8)));
        }
        const auto knob = toView(rotationKnob());
        p.setPen(Qt::white);
        p.drawLine(toView((quad[0] + quad[1]) / 2), knob);
        p.setBrush(QColor("#e8893a"));
        p.drawEllipse(knob, 5, 5);
    }
    void mousePressEvent(QMouseEvent *e) override {
        previous = toImage(e->position());
        handle = -1;
        for (int i = 0; i < 4; ++i)
            if (QLineF(toView(quad[i]), e->position()).length() < 12)
                handle = i;
        if (QLineF(toView(rotationKnob()), e->position()).length() < 14)
            handle = 4;
        if (handle == -1 && !quad.containsPoint(previous, Qt::OddEvenFill))
            handle = -2;
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (!(e->buttons() & Qt::LeftButton) || handle == -2)
            return;
        const auto p = toImage(e->position());
        const auto delta = p - previous;
        previous = p;
        if (handle == -1) {
            for (auto &q : quad)
                q += delta;
        } else if (handle == 4 || mode == "Rotate") {
            const auto center = quad.boundingRect().center();
            const auto a = p - center;
            const auto b = p - delta - center;
            double angle = qRadiansToDegrees(std::atan2(a.y(), a.x()) - std::atan2(b.y(), b.x()));
            if (e->modifiers() & Qt::ShiftModifier)
                angle = std::round(angle / 15.) * 15.;
            QTransform t;
            t.translate(center.x(), center.y());
            t.rotate(angle);
            t.translate(-center.x(), -center.y());
            quad = t.map(quad);
        } else if (mode == "Distort") {
            quad[handle] = p;
        } else if (mode == "Perspective") {
            const int paired = handle ^ 1;
            quad[handle] += delta;
            quad[paired] += QPointF(-delta.x(), delta.y());
        } else if (mode == "Skew") {
            const int paired = handle ^ 1;
            quad[handle] += delta;
            quad[paired] += delta;
        } else {
            const int opposite = (handle + 2) % 4;
            const auto fixed = quad[opposite];
            QPointF moved = p;
            if (e->modifiers() & Qt::ShiftModifier) {
                const auto v = p - fixed;
                const double ratio = image.width() / double(qMax(1, image.height()));
                moved.setY(fixed.y() + std::copysign(std::abs(v.x()) / ratio, v.y()));
            }
            const QRectF box = QRectF(fixed, moved).normalized();
            quad = {box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()};
        }
        QTransform t;
        valid = QTransform::quadToQuad(original(), quad, t) && t.isInvertible();
        update();
        if (changed)
            changed();
    }
    void mouseReleaseEvent(QMouseEvent *) override { handle = -2; }
};
TransformDialog::TransformDialog(const QImage &source, const QString &initialMode, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(initialMode);
    setObjectName("transformDialog");
    resize(1000, 700);
    auto *outer = new QVBoxLayout(this);
    auto *row = new QHBoxLayout;
    outer->addLayout(row, 1);
    m_preview = new Preview(source, this);
    row->addWidget(m_preview, 1);
    auto *controls = new QWidget;
    controls->setFixedWidth(230);
    auto *form = new QFormLayout(controls);
    row->addWidget(controls);
    auto *mode = new QComboBox;
    mode->setObjectName("transformMode");
    mode->addItems({"Free Transform", "Scale", "Rotate", "Skew", "Distort", "Perspective"});
    mode->setCurrentText(initialMode);
    m_preview->mode = mode->currentText();
    form->addRow("Mode", mode);
    auto field = [&](const QString &label, const QString &id, double min, double max, double initial) {
        auto *s = new QDoubleSpinBox;
        s->setObjectName(id);
        s->setRange(min, max);
        s->setDecimals(2);
        s->setValue(initial);
        form->addRow(label, s);
        return s;
    };
    auto *w = field("Width (%)", "widthPercent", -10000, 10000, 100),
         *h = field("Height (%)", "heightPercent", -10000, 10000, 100),
         *angle = field("Rotation (°)", "rotation", -360, 360, 0),
         *sx = field("Horizontal skew", "skewX", -10, 10, 0),
         *sy = field("Vertical skew", "skewY", -10, 10, 0);
    auto update = [=, this] {
        QTransform t;
        t.translate(source.width() / 2., source.height() / 2.);
        t.rotate(angle->value());
        t.shear(sx->value(), sy->value());
        t.scale(w->value() / 100., h->value() / 100.);
        t.translate(-source.width() / 2., -source.height() / 2.);
        m_preview->quad = t.map(m_preview->original());
        m_preview->valid = t.isInvertible();
        m_preview->update();
    };
    for (auto *s : {w, h, angle, sx, sy})
        connect(s, &QDoubleSpinBox::valueChanged, this, update);
    connect(mode, &QComboBox::currentTextChanged, this, [this](const QString &s) { m_preview->mode = s; });
    auto *hint = new QLabel("Drag corners to transform. Drag inside to move. Drag the round handle to "
                            "rotate. Shift constrains proportions.");
    hint->setWordWrap(true);
    form->addRow(hint);
    auto *reset = new QPushButton("Reset");
    form->addRow(reset);
    connect(reset, &QPushButton::clicked, this, [=, this] {
        w->setValue(100);
        h->setValue(100);
        angle->setValue(0);
        sx->setValue(0);
        sy->setValue(0);
        m_preview->reset();
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    outer->addWidget(buttons);
    m_preview->changed = [this, buttons] {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(m_preview->valid);
    };
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_preview->valid)
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
QTransform TransformDialog::resultTransform() const { return m_preview->matrix(); }
} // namespace serika
