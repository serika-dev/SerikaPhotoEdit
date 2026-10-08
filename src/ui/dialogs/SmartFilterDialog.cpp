#include "SmartFilterDialog.h"
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <memory>
namespace serika {
SmartFilterDialog::SmartFilterDialog(Document *document, QWidget *parent) : QDialog(parent) {
    setWindowTitle("Edit Smart Filters");
    resize(840, 540);
    m_filters = document->activeLayer()->smartFilters;
    auto *outer = new QVBoxLayout(this);
    auto *hint = new QLabel("Filters keep the original pixels. Changes apply together when you press OK.");
    hint->setWordWrap(true);
    outer->addWidget(hint);
    auto *body = new QHBoxLayout;
    outer->addLayout(body, 1);
    auto *stack = new QVBoxLayout;
    body->addLayout(stack);
    auto *list = new QListWidget;
    list->setObjectName("smartFilterList");
    list->setMinimumWidth(200);
    stack->addWidget(list, 1);
    auto *order = new QHBoxLayout;
    stack->addLayout(order);
    auto *up = new QPushButton("Up"), *down = new QPushButton("Down"), *remove = new QPushButton("Remove");
    order->addWidget(up);
    order->addWidget(down);
    order->addWidget(remove);
    auto *details = new QVBoxLayout;
    body->addLayout(details, 2);
    auto *preview = new QLabel;
    preview->setObjectName("smartFilterPreview");
    preview->setMinimumSize(350, 220);
    preview->setAlignment(Qt::AlignCenter);
    details->addWidget(preview, 1);
    auto *formWidget = new QWidget;
    auto *form = new QFormLayout(formWidget);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(formWidget);
    details->addWidget(scroll, 1);
    auto *enabled = new QCheckBox("Enabled");
    enabled->setObjectName("filterEnabled");
    form->addRow(enabled);
    auto *opacity = new QDoubleSpinBox;
    opacity->setObjectName("filterOpacity");
    opacity->setRange(0, 100);
    opacity->setSuffix(" %");
    form->addRow("Opacity", opacity);
    auto *blend = new QComboBox;
    blend->setObjectName("filterBlendMode");
    blend->addItems(blendModeNames());
    form->addRow("Blend mode", blend);
    // Keep uncommon existing parameters editable as well as the standard controls.
    QMap<QString, double> defaults{{"radius", 4}, {"amount", 50}, {"angle", 0}, {"size", 12}};
    for (const auto &value : m_filters)
        for (const auto &key : value.toObject()["parameters"].toObject().keys())
            if (value.toObject()["parameters"].toObject()[key].isDouble())
                defaults.insert(key, value.toObject()["parameters"].toObject()[key].toDouble());
    QMap<QString, QDoubleSpinBox *> controls;
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        auto *spin = new QDoubleSpinBox;
        spin->setObjectName("filter_" + it.key());
        spin->setRange(it.key() == "radius" ? .1
                       : it.key() == "size" ? 1
                                            : -1000,
                       it.key() == "radius" ? 100
                       : it.key() == "size" ? 200
                                            : 1000);
        spin->setDecimals(2);
        form->addRow(it.key(), spin);
        controls[it.key()] = spin;
    }
    auto loading = std::make_shared<bool>(false);
    Layer original = *document->activeLayer();
    original.pixels.setImage(
        original.pixels.image().scaled(400, 230, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    original.parameters.remove("contentTransform");
    auto render = [this, original, preview, document] {
        Layer layer = original;
        layer.smartFilters = m_filters;
        preview->setPixmap(QPixmap::fromImage(document->layerImage(layer)));
    };
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, render);
    auto refreshList = [this, list, loading](int selected) {
        *loading = true;
        list->clear();
        for (const auto &entry : m_filters) {
            auto object = entry.toObject();
            list->addItem(object["name"].toString() + (object["enabled"].toBool(true) ? "" : " (off)"));
        }
        *loading = false;
        list->setCurrentRow(selected);
    };
    connect(list, &QListWidget::currentRowChanged, this,
            [this, loading, enabled, opacity, blend, controls, defaults, formWidget](int row) {
                if (*loading)
                    return;
                formWidget->setEnabled(row >= 0);
                if (row < 0)
                    return;
                *loading = true;
                const auto entry = m_filters[row].toObject();
                const auto params = entry["parameters"].toObject();
                enabled->setChecked(entry["enabled"].toBool(true));
                opacity->setValue(entry["opacity"].toDouble(1) * 100);
                blend->setCurrentText(entry["blendMode"].toString("Normal"));
                for (auto it = controls.begin(); it != controls.end(); ++it)
                    it.value()->setValue(params[it.key()].toDouble(defaults[it.key()]));
                *loading = false;
            });
    auto update = [this, list, loading, controls, enabled, opacity, blend, timer](const QString &key) {
        const int row = list->currentRow();
        if (*loading || row < 0)
            return;
        auto entry = m_filters[row].toObject();
        auto params = entry["parameters"].toObject();
        if (controls.contains(key))
            params[key] = controls[key]->value();
        entry["parameters"] = params;
        entry["enabled"] = enabled->isChecked();
        entry["opacity"] = opacity->value() / 100.;
        entry["blendMode"] = blend->currentText();
        m_filters[row] = entry;
        list->item(row)->setText(entry["name"].toString() + (enabled->isChecked() ? "" : " (off)"));
        timer->start();
    };
    for (auto it = controls.begin(); it != controls.end(); ++it)
        connect(it.value(), &QDoubleSpinBox::valueChanged, this, [update, key = it.key()] { update(key); });
    connect(enabled, &QCheckBox::toggled, this, [update] { update({}); });
    connect(opacity, &QDoubleSpinBox::valueChanged, this, [update] { update({}); });
    connect(blend, &QComboBox::currentTextChanged, this, [update] { update({}); });
    auto move = [this, list, refreshList, timer](int delta) {
        int row = list->currentRow(), target = row + delta;
        if (row < 0 || target < 0 || target >= m_filters.size())
            return;
        auto entry = m_filters.takeAt(row);
        m_filters.insert(target, entry);
        refreshList(target);
        timer->start();
    };
    connect(up, &QPushButton::clicked, this, [move] { move(-1); });
    connect(down, &QPushButton::clicked, this, [move] { move(1); });
    connect(remove, &QPushButton::clicked, this, [this, list, refreshList, timer] {
        int row = list->currentRow();
        if (row < 0)
            return;
        m_filters.removeAt(row);
        refreshList(qMin(row, int(m_filters.size()) - 1));
        timer->start();
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    refreshList(m_filters.isEmpty() ? -1 : 0);
    render();
}
} // namespace serika
