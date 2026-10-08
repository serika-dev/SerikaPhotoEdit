#pragma once
#include <QDropEvent>
#include <QTreeWidget>
#include <functional>
namespace serika {
class LayerTree final : public QTreeWidget {
  public:
    using QTreeWidget::QTreeWidget;
    std::function<void()> orderChanged;
    bool dragging = false;

  protected:
    void startDrag(Qt::DropActions actions) override {
        dragging = true;
        QTreeWidget::startDrag(actions);
        dragging = false;
    }
    void dropEvent(QDropEvent *event) override {
        QTreeWidget::dropEvent(event);
        if (orderChanged)
            orderChanged();
    }
};
} // namespace serika
