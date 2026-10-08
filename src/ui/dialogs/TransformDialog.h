#pragma once
#include <QDialog>
#include <QImage>
#include <QTransform>
namespace serika {
class TransformDialog : public QDialog {
    Q_OBJECT
  public:
    TransformDialog(const QImage &source, const QString &mode = "Free Transform", QWidget *parent = nullptr);
    QTransform resultTransform() const;

  private:
    class Preview;
    Preview *m_preview;
};
} // namespace serika
