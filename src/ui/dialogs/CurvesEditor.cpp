#include "CurvesEditor.h"
#include "document/Document.h"
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace serika {
namespace {
const QStringList keys{"points", "redPoints", "greenPoints", "bluePoints"};
QJsonArray identity() { return {QJsonArray{0, 0}, QJsonArray{255, 255}}; }
QPolygonF normalizePoints(const QJsonArray &values) {
    QMap<qreal, qreal> sorted;
    for (const auto &entry : values) {
        const auto pair = entry.toArray();
        if (pair.size() != 2 || !pair[0].isDouble() || !pair[1].isDouble())
            continue;
        const double x = pair[0].toDouble(), y = pair[1].toDouble();
        if (std::isfinite(x) && std::isfinite(y))
            sorted[std::clamp(x, 0., 255.)] = std::clamp(y, 0., 255.);
    }
    if (sorted.size() < 2)
        sorted = {{0, 0}, {255, 255}};
    // Preserve supplied endpoints. Inserting plateau endpoints changes Hermite tangents.
    QPolygonF points;
    for (auto it = sorted.begin(); it != sorted.end(); ++it)
        points << QPointF(it.key(), it.value());
    return points;
}
QJsonArray encode(const QPolygonF &points) {
    QJsonArray result;
    for (auto point : points)
        result.append(QJsonArray{point.x(), point.y()});
    return result;
}
} // namespace
class CurveGraph final : public QWidget {
  public:
    explicit CurveGraph(QWidget *parent) : QWidget(parent) {
        setObjectName("curveGraph");
        setMinimumSize(300, 220);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName("Tone curve. Click to add points, drag to adjust, Delete to remove a point.");
    }
    QPolygonF points{QPointF(0, 0), QPointF(255, 255)};
    std::array<std::array<int, 256>, 4> histogram{};
    int selected = 0, channel = 0;
    std::function<void()> edited, selectionChanged;
    QRectF plot() const { return QRectF(rect()).adjusted(18, 12, -14, -20); }
    QPointF pixel(QPointF p) const {
        const auto r = plot();
        return {r.left() + p.x() / 255 * r.width(), r.bottom() - p.y() / 255 * r.height()};
    }
    void setPoint(QPointF point) {
        if (selected < 0 || selected >= points.size())
            return;
        const qreal lo = selected == 0 ? 0 : points[selected - 1].x() + 1e-6;
        const qreal hi = selected == points.size() - 1 ? 255 : points[selected + 1].x() - 1e-6;
        point.setX(lo <= hi ? std::clamp(point.x(), lo, hi) : points[selected].x());
        point.setY(std::clamp(point.y(), qreal(0), qreal(255)));
        if (points[selected] == point)
            return;
        points[selected] = point;
        update();
        if (edited)
            edited();
    }
    void removePoint() {
        if (selected <= 0 || selected >= points.size() - 1)
            return;
        points.remove(selected);
        selected = std::min(selected, int(points.size()) - 1);
        update();
        if (edited)
            edited();
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto r = plot();
        p.fillRect(rect(), palette().base());
        p.setPen(palette().mid().color());
        for (int i = 0; i <= 4; ++i) {
            const qreal x = r.left() + r.width() * i / 4, y = r.top() + r.height() * i / 4;
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        }
        const auto &bins = histogram[channel];
        const int maximum = *std::max_element(bins.begin(), bins.end());
        QPainterPath bars;
        bars.moveTo(r.bottomLeft());
        for (int i = 0; i < 256; ++i)
            bars.lineTo(r.left() + i / 255. * r.width(),
                        r.bottom() - (maximum ? std::sqrt(bins[i] / double(maximum)) : 0) * r.height());
        bars.lineTo(r.bottomRight());
        QColor shade = palette().text().color();
        shade.setAlpha(30);
        p.fillPath(bars, shade);
        p.setPen(QPen(palette().mid().color(), 1, Qt::DashLine));
        p.drawLine(r.bottomLeft(), r.topRight());
        QImage ramp(256, 1, QImage::Format_RGBA64);
        for (int i = 0; i < 256; ++i)
            ramp.setPixelColor(i, 0, QColor(i, i, i));
        const auto evaluated = applyAdjustment(ramp, "Curves", {{"points", encode(points)}});
        QPainterPath curve;
        for (int i = 0; i < 256; ++i) {
            const auto pt = pixel({qreal(i), qreal(evaluated.pixelColor(i, 0).redF() * 255)});
            if (!i)
                curve.moveTo(pt);
            else
                curve.lineTo(pt);
        }
        const bool dark = palette().base().color().lightnessF() < .5;
        const QColor color = channel == 1   ? QColor(dark ? "#ed8181" : "#af233e")
                             : channel == 2 ? QColor(dark ? "#65c895" : "#21764c")
                             : channel == 3 ? QColor(dark ? "#83aaff" : "#235cae")
                                            : palette().highlight().color();
        p.setPen(QPen(color, 2));
        p.drawPath(curve);
        for (int i = 0; i < points.size(); ++i) {
            p.setBrush(i == selected ? color : palette().base().color());
            p.drawEllipse(pixel(points[i]), i == selected ? 5 : 4, i == selected ? 5 : 4);
        }
        if (hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(palette().highlight().color());
            p.drawRect(rect().adjusted(1, 1, -2, -2));
        }
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton || !plot().contains(event->position()))
            return;
        setFocus();
        selected = -1;
        for (int i = 0; i < points.size(); ++i)
            if (QLineF(pixel(points[i]), event->position()).length() < 10) {
                selected = i;
                break;
            }
        if (selected < 0) {
            const int x =
                std::clamp(qRound((event->position().x() - plot().left()) / plot().width() * 255), 1, 254);
            int index = 0;
            while (index < points.size() && points[index].x() < x)
                ++index;
            if (index == points.size() || points[index].x() != x)
                points.insert(
                    index, {qreal(x), 255 - (event->position().y() - plot().top()) / plot().height() * 255});
            selected = index;
            if (edited)
                edited();
        }
        update();
        if (selectionChanged)
            selectionChanged();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (!(event->buttons() & Qt::LeftButton))
            return;
        setPoint({qreal(qRound((event->position().x() - plot().left()) / plot().width() * 255)),
                  qreal(qRound(255 - (event->position().y() - plot().top()) / plot().height() * 255))});
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
            removePoint();
            return;
        }
        const int step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
        QPointF delta;
        if (event->key() == Qt::Key_Left)
            delta.setX(-step);
        else if (event->key() == Qt::Key_Right)
            delta.setX(step);
        else if (event->key() == Qt::Key_Up)
            delta.setY(step);
        else if (event->key() == Qt::Key_Down)
            delta.setY(-step);
        else {
            QWidget::keyPressEvent(event);
            return;
        }
        if (selected >= 0)
            setPoint(points[selected] + delta);
    }
};
CurvesEditor::CurvesEditor(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *top = new QHBoxLayout;
    m_channel = new QComboBox;
    m_channel->setObjectName("curveChannel");
    m_channel->addItems({"RGB", "Red", "Green", "Blue"});
    top->addWidget(new QLabel("Channel"));
    top->addWidget(m_channel);
    top->addStretch();
    auto *presets = new QComboBox;
    presets->setObjectName("curvePreset");
    presets->addItems({"Custom", "Linear", "Medium contrast", "Lighten", "Darken", "Negative"});
    top->addWidget(presets);
    layout->addLayout(top);
    m_graph = new CurveGraph(this);
    layout->addWidget(m_graph, 1);
    auto *row = new QHBoxLayout;
    m_input = new QSpinBox;
    m_input->setObjectName("curveInput");
    m_input->setRange(0, 255);
    m_output = new QSpinBox;
    m_output->setObjectName("curveOutput");
    m_output->setRange(0, 255);
    row->addWidget(new QLabel("Input"));
    row->addWidget(m_input);
    row->addWidget(new QLabel("Output"));
    row->addWidget(m_output);
    auto *remove = new QPushButton("Delete point");
    remove->setObjectName("curveDeletePoint");
    row->addWidget(remove);
    layout->addLayout(row);
    auto *files = new QHBoxLayout;
    auto *load = new QPushButton("Load curve…");
    auto *save = new QPushButton("Save curve…");
    auto *reset = new QPushButton("Reset all channels");
    files->addWidget(load);
    files->addWidget(save);
    files->addStretch();
    files->addWidget(reset);
    layout->addLayout(files);
    m_graph->selectionChanged = [this] { updateFields(); };
    m_graph->edited = [this, presets] {
        QSignalBlocker b(presets);
        presets->setCurrentIndex(0);
        storeCurve();
    };
    connect(m_channel, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_graph->channel = index;
        m_graph->points = normalizePoints(m_parameters.value(keys[index]).toArray());
        m_graph->selected = 0;
        updateFields();
        m_graph->update();
    });
    connect(m_input, &QSpinBox::valueChanged, this,
            [this](int value) { m_graph->setPoint({qreal(value), qreal(m_output->value())}); });
    connect(m_output, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_graph->selected >= 0 && m_graph->selected < m_graph->points.size())
            m_graph->setPoint({m_graph->points[m_graph->selected].x(), qreal(value)});
    });
    connect(remove, &QPushButton::clicked, this, [this] { m_graph->removePoint(); });
    connect(reset, &QPushButton::clicked, this, [this] {
        setParameters({});
        emit parametersChanged();
    });
    connect(presets, &QComboBox::activated, this, [this](int index) {
        if (!index)
            return;
        QJsonArray points = identity();
        if (index == 2)
            points = {QJsonArray{0, 0}, QJsonArray{64, 45}, QJsonArray{192, 210}, QJsonArray{255, 255}};
        if (index == 3)
            points = {QJsonArray{0, 0}, QJsonArray{128, 165}, QJsonArray{255, 255}};
        if (index == 4)
            points = {QJsonArray{0, 0}, QJsonArray{128, 90}, QJsonArray{255, 255}};
        if (index == 5)
            points = {QJsonArray{0, 255}, QJsonArray{255, 0}};
        m_graph->points = normalizePoints(points);
        m_graph->selected = 0;
        storeCurve();
    });
    connect(save, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getSaveFileName(this, "Save curve preset", {}, "Serika curves (*.specurve)");
        if (path.isEmpty())
            return;
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(QJsonDocument(QJsonObject{{"schema", 1}, {"curves", parameters()}}).toJson()) < 0 ||
            !file.commit())
            QMessageBox::warning(this, "Save curve", file.errorString());
    });
    connect(load, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getOpenFileName(this, "Load curve preset", {}, "Serika curves (*.specurve)");
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
            QMessageBox::warning(this, "Load curve", "Could not read this curve preset.");
            return;
        }
        const auto data = QJsonDocument::fromJson(file.readAll()).object();
        if (data.value("schema").toInt() != 1 || !data.value("curves").isObject()) {
            QMessageBox::warning(this, "Load curve", "This file is not a Serika curve preset.");
            return;
        }
        setParameters(data.value("curves").toObject());
        emit parametersChanged();
    });
    setParameters({});
}
void CurvesEditor::setParameters(const QJsonObject &parameters) {
    m_parameters = parameters;
    for (const auto &key : keys)
        m_parameters[key] = encode(normalizePoints(parameters.value(key).toArray()));
    m_graph->points = normalizePoints(m_parameters.value(keys[m_channel->currentIndex()]).toArray());
    m_graph->selected = 0;
    updateFields();
    m_graph->update();
}
QJsonObject CurvesEditor::parameters() const { return m_parameters; }
void CurvesEditor::storeCurve() {
    m_parameters[keys[m_channel->currentIndex()]] = encode(m_graph->points);
    updateFields();
    m_graph->update();
    emit parametersChanged();
}
void CurvesEditor::updateFields() {
    QSignalBlocker a(m_input), b(m_output);
    const int i = m_graph->selected;
    if (i < 0 || i >= m_graph->points.size())
        return;
    const int minimum = i == 0 ? 0 : int(std::ceil(m_graph->points[i - 1].x() + 1e-6));
    const int maximum =
        i == m_graph->points.size() - 1 ? 255 : int(std::floor(m_graph->points[i + 1].x() - 1e-6));
    m_input->setRange(minimum <= maximum ? minimum : 0, minimum <= maximum ? maximum : 255);
    m_input->setEnabled(minimum <= maximum);
    m_input->setValue(qRound(m_graph->points[i].x()));
    m_output->setValue(qRound(m_graph->points[i].y()));
}
void CurvesEditor::setSource(const QImage &source) {
    m_graph->histogram = {};
    const auto sample = source.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                            .convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < sample.height(); ++y)
        for (int x = 0; x < sample.width(); ++x) {
            const auto c = sample.pixelColor(x, y);
            if (!c.alpha())
                continue;
            ++m_graph->histogram[0][qGray(c.rgb())];
            ++m_graph->histogram[1][c.red()];
            ++m_graph->histogram[2][c.green()];
            ++m_graph->histogram[3][c.blue()];
        }
    m_graph->update();
}
} // namespace serika
