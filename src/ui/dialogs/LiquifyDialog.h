#pragma once
#include <QDialog>
#include <QImage>
namespace serika {
class LiquifyDialog final : public QDialog {
  public:
    // A null image means that the user canceled. Output retains the input pixel depth.
    static QImage edit(const QImage &image, QWidget *parent = nullptr);
};
} // namespace serika
