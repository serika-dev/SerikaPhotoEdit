#pragma once
#include "document/Document.h"
#include <QDialog>
namespace serika {
class SmartFilterDialog : public QDialog {
  public:
    SmartFilterDialog(Document *document, QWidget *parent = nullptr);
    QJsonArray filters() const { return m_filters; }

  private:
    QJsonArray m_filters;
};
} // namespace serika
