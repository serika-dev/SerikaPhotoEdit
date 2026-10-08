#include "ui/MainWindow.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace serika {
void MainWindow::buildCropOptions() {
    auto add = [this](QWidget *widget) {
        auto *action = m_options->addWidget(widget);
        action->setProperty("cropOption", true);
        return action;
    };
    auto *preset = new QComboBox;
    preset->setObjectName("cropPreset");
    preset->addItems({"Ratio", "Original ratio", "1 : 1", "4 : 5", "5 : 7", "16 : 9", "W × H × Resolution"});
    preset->setFixedWidth(135);
    add(preset);
    for (const QString &name : {QString("cropWidth"), QString("cropHeight")}) {
        auto *dimension = new QDoubleSpinBox;
        dimension->setObjectName(name);
        dimension->setRange(0, 300000);
        dimension->setDecimals(2);
        dimension->setSpecialValueText(name == "cropWidth" ? "Width" : "Height");
        dimension->setFixedWidth(75);
        dimension->setToolTip("Ratio component, or output pixels in W × H × Resolution mode");
        add(dimension);
        connect(dimension, &QDoubleSpinBox::valueChanged, this, [this] { applyCropOptions(); });
    }
    auto *swap = new QToolButton;
    swap->setObjectName("cropSwapRatio");
    swap->setText("⇄");
    swap->setToolTip("Swap crop width and height");
    add(swap);
    connect(swap, &QToolButton::clicked, this, [this] {
        auto *width = m_options->findChild<QDoubleSpinBox *>("cropWidth");
        auto *height = m_options->findChild<QDoubleSpinBox *>("cropHeight");
        qreal previous = width->value();
        QSignalBlocker blockWidth(width), blockHeight(height);
        width->setValue(height->value());
        height->setValue(previous);
        applyCropOptions();
    });
    auto *resolution = new QDoubleSpinBox;
    resolution->setObjectName("cropResolution");
    resolution->setRange(0, 12000);
    resolution->setDecimals(0);
    resolution->setSpecialValueText("DPI");
    resolution->setSuffix(" ppi");
    resolution->setFixedWidth(75);
    add(resolution);
    connect(resolution, &QDoubleSpinBox::valueChanged, this, [this] { applyCropOptions(); });
    auto *straighten = new QToolButton;
    straighten->setText("Straighten");
    straighten->setToolTip("Drag along a horizon or vertical edge, then commit the crop");
    add(straighten);
    connect(straighten, &QToolButton::clicked, this, [this] {
        if (auto *canvas = currentCanvas()) {
            canvas->beginStraighten();
            canvas->setFocus();
        }
    });
    auto *automatic = new QToolButton;
    automatic->setText("Auto");
    automatic->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(automatic);
    menu->addAction("Trim transparent edges", [this] {
        if (auto *canvas = currentCanvas())
            canvas->autoCropTransparent();
    });
    menu->addAction("Detect content edges", [this] {
        if (auto *canvas = currentCanvas())
            canvas->autoCropContent();
    });
    menu->addAction("Detect and straighten horizon", [this] {
        if (auto *canvas = currentCanvas())
            canvas->autoStraighten();
    });
    automatic->setMenu(menu);
    add(automatic);
    auto *overlay = new QComboBox;
    overlay->setObjectName("cropOverlay");
    overlay->addItems({"Thirds", "Grid", "Diagonal", "Golden Ratio", "None"});
    overlay->setFixedWidth(103);
    overlay->setToolTip("Crop composition overlay");
    add(overlay);
    connect(overlay, &QComboBox::currentTextChanged, this, [this] { applyCropOptions(); });
    auto *remove = new QCheckBox("Delete cropped pixels");
    remove->setObjectName("deleteCroppedPixels");
    remove->setToolTip("Unchecked retains out-of-canvas layer pixels for Reveal All");
    add(remove);
    connect(remove, &QCheckBox::toggled, this, [this] { applyCropOptions(); });
    for (const QString &name : {QString("Reset"), QString("Cancel"), QString("Apply")}) {
        auto *button = new QToolButton;
        button->setText(name);
        button->setObjectName("crop" + name);
        add(button);
        connect(button, &QToolButton::clicked, this, [this, name] {
            if (auto *canvas = currentCanvas()) {
                if (name == "Reset")
                    canvas->resetCrop();
                else if (name == "Cancel")
                    canvas->cancelCrop();
                else
                    canvas->commitCrop();
                canvas->setFocus();
            }
        });
    }
    connect(preset, &QComboBox::currentIndexChanged, this, [this](int index) {
        auto *width = m_options->findChild<QDoubleSpinBox *>("cropWidth");
        auto *height = m_options->findChild<QDoubleSpinBox *>("cropHeight");
        QSignalBlocker blockWidth(width), blockHeight(height);
        QSizeF ratio;
        if (index == 1 && currentDocument())
            ratio = QSizeF(currentDocument()->state.size);
        if (index == 2)
            ratio = QSizeF(1, 1);
        if (index == 3)
            ratio = QSizeF(4, 5);
        if (index == 4)
            ratio = QSizeF(5, 7);
        if (index == 5)
            ratio = QSizeF(16, 9);
        if (index != 6) {
            width->setValue(ratio.isValid() ? ratio.width() : 0);
            height->setValue(ratio.isValid() ? ratio.height() : 0);
        } else if (currentDocument()) {
            width->setValue(currentDocument()->state.size.width());
            height->setValue(currentDocument()->state.size.height());
            m_options->findChild<QDoubleSpinBox *>("cropResolution")
                ->setValue(currentDocument()->state.resolution);
        }
        applyCropOptions();
    });
}
void MainWindow::applyCropOptions() {
    auto *canvas = currentCanvas();
    if (!canvas || !m_options->findChild<QComboBox *>("cropPreset"))
        return;
    CropSettings settings = canvas->cropSettings();
    const qreal width = m_options->findChild<QDoubleSpinBox *>("cropWidth")->value();
    const qreal height = m_options->findChild<QDoubleSpinBox *>("cropHeight")->value();
    bool resample = m_options->findChild<QComboBox *>("cropPreset")->currentIndex() == 6;
    settings.ratio = width > 0 && height > 0 ? QSizeF(width, height) : QSizeF();
    settings.outputSize =
        resample && width > 0 && height > 0 ? QSize(qRound(width), qRound(height)) : QSize();
    settings.resolution = resample ? m_options->findChild<QDoubleSpinBox *>("cropResolution")->value() : 0;
    settings.deleteCroppedPixels = m_options->findChild<QCheckBox *>("deleteCroppedPixels")->isChecked();
    settings.overlay = m_options->findChild<QComboBox *>("cropOverlay")->currentText();
    canvas->setCropSettings(settings);
}
void MainWindow::addMaskProperties(Document *document, quint64 layerId, bool vector) {
    int index = document->indexForId(layerId);
    if (index < 0)
        return;
    const Layer &layer = document->state.layers[index];
    auto *group = new QGroupBox(vector ? "Vector mask" : "Layer mask", m_properties);
    auto *form = new QFormLayout(group);
    form->setContentsMargins(6, 6, 6, 6);
    auto edit = [guard = QPointer<Document>(document),
                 layerId](const QString &name, const std::function<void(Layer &)> &operation) {
        if (!guard)
            return;
        int index = guard->indexForId(layerId);
        if (index >= 0)
            guard->mutate(name, [guard, index, operation] { operation(guard->state.layers[index]); });
    };
    auto *enabled = new QCheckBox("Enabled");
    enabled->setObjectName(vector ? "vectorMaskEnabled" : "maskEnabled");
    enabled->setChecked(vector ? layer.vectorMaskEnabled : layer.maskEnabled);
    form->addRow(enabled);
    connect(enabled, &QCheckBox::toggled, group, [edit, vector](bool value) {
        edit("Mask enabled", [vector, value](Layer &layer) {
            if (vector)
                layer.vectorMaskEnabled = value;
            else
                layer.maskEnabled = value;
        });
    });
    if (!vector) {
        auto *target = new QCheckBox("Paint on mask");
        target->setChecked(layer.maskTarget);
        target->setObjectName("maskTarget");
        form->addRow(target);
        connect(target, &QCheckBox::toggled, group,
                [this, guard = QPointer<Document>(document), layerId](bool value) {
                    if (!guard)
                        return;
                    int index = guard->indexForId(layerId);
                    if (index >= 0)
                        guard->state.layers[index].maskTarget = value;
                    if (auto *canvas = currentCanvas())
                        canvas->update();
                });
    }
    auto *density = new QDoubleSpinBox;
    density->setObjectName(vector ? "vectorMaskDensity" : "maskDensity");
    density->setRange(0, 100);
    density->setSuffix("%");
    density->setValue(100 * (vector ? layer.vectorMaskDensity : layer.maskDensity));
    form->addRow("Density", density);
    connect(density, &QDoubleSpinBox::editingFinished, group, [edit, vector, density] {
        qreal value = density->value() / 100;
        edit("Mask density", [vector, value](Layer &layer) {
            if (vector)
                layer.vectorMaskDensity = value;
            else
                layer.maskDensity = value;
        });
    });
    auto *feather = new QDoubleSpinBox;
    feather->setObjectName(vector ? "vectorMaskFeather" : "maskFeather");
    feather->setRange(0, 1000);
    feather->setDecimals(1);
    feather->setSuffix(" px");
    feather->setValue(vector ? layer.vectorMaskFeather : layer.maskFeather);
    form->addRow("Feather", feather);
    connect(feather, &QDoubleSpinBox::editingFinished, group, [edit, vector, feather] {
        qreal value = feather->value();
        edit("Mask feather", [vector, value](Layer &layer) {
            if (vector)
                layer.vectorMaskFeather = value;
            else
                layer.maskFeather = value;
        });
    });
    auto *linked = new QCheckBox("Link to layer");
    linked->setObjectName(vector ? "vectorMaskLinked" : "maskLinked");
    linked->setChecked(vector ? layer.vectorMaskLinked : layer.maskLinked);
    form->addRow(linked);
    connect(linked, &QCheckBox::toggled, group, [edit, vector](bool value) {
        edit("Mask linking", [vector, value](Layer &layer) {
            if (vector)
                layer.vectorMaskLinked = value;
            else
                layer.maskLinked = value;
        });
    });
    auto *preview = new QComboBox;
    preview->addItems({"Composite", "Mask only", "Overlay"});
    if (auto *canvas = currentCanvas())
        preview->setCurrentIndex(int(canvas->maskPreview()));
    form->addRow("View", preview);
    connect(preview, &QComboBox::currentIndexChanged, group, [this](int value) {
        if (auto *canvas = currentCanvas())
            canvas->setMaskPreview(CanvasView::MaskPreview(value));
    });
    m_properties->layout()->addWidget(group);
}
void MainWindow::addSmartFilterProperties(Document *document, quint64 layerId) {
    int layerIndex = document->indexForId(layerId);
    if (layerIndex < 0)
        return;
    auto *group = new QGroupBox("Smart Filters", m_properties);
    auto *layout = new QVBoxLayout(group);
    layout->setContentsMargins(6, 6, 6, 6);
    QJsonArray filters = document->state.layers[layerIndex].smartFilters;
    for (qsizetype index = 0; index < filters.size(); ++index) {
        auto *row = new QWidget;
        auto *horizontal = new QHBoxLayout(row);
        horizontal->setContentsMargins(0, 0, 0, 0);
        horizontal->setSpacing(2);
        const auto filter = filters[index].toObject();
        auto *enabled = new QCheckBox(filter.value("name").toString());
        enabled->setChecked(filter.value("enabled").toBool(true));
        horizontal->addWidget(enabled, 1);
        auto mutate = [guard = QPointer<Document>(document), layerId,
                       index](const QString &name, const std::function<void(QJsonArray &, int)> &operation) {
            if (!guard)
                return;
            int layerIndex = guard->indexForId(layerId);
            if (layerIndex < 0 || index >= guard->state.layers[layerIndex].smartFilters.size())
                return;
            guard->mutate(name, [guard, layerIndex, index, operation] {
                auto &stack = guard->state.layers[layerIndex].smartFilters;
                operation(stack, int(index));
            });
        };
        connect(enabled, &QCheckBox::toggled, group, [mutate](bool value) {
            mutate("Toggle smart filter", [value](QJsonArray &stack, int index) {
                auto object = stack[index].toObject();
                object["enabled"] = value;
                stack[index] = object;
            });
        });
        for (const QString &label : {QString("↑"), QString("↓"), QString("×")}) {
            auto *button = new QToolButton;
            button->setText(label);
            button->setToolTip(label == "×"   ? "Remove filter"
                               : label == "↑" ? "Move filter earlier"
                                              : "Move filter later");
            button->setEnabled(label == "×" || (label == "↑" ? index > 0 : index + 1 < filters.size()));
            horizontal->addWidget(button);
            connect(button, &QToolButton::clicked, group, [mutate, label] {
                mutate("Edit smart filter stack", [label](QJsonArray &stack, int index) {
                    if (label == "×")
                        stack.removeAt(index);
                    else {
                        int destination = index + (label == "↑" ? -1 : 1);
                        if (destination >= 0 && destination < stack.size()) {
                            auto entry = stack.takeAt(index);
                            stack.insert(destination, entry);
                        }
                    }
                });
            });
        }
        layout->addWidget(row);
    }
    auto *edit = new QPushButton("Edit filter parameters...");
    layout->addWidget(edit);
    connect(edit, &QPushButton::clicked, this, [this] { runCommand("Edit Smart Filters..."); });
    m_properties->layout()->addWidget(group);
}
} // namespace serika
