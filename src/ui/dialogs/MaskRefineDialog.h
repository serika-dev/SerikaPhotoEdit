#pragma once
#include <QDialog>
#include <QImage>
namespace serika {
class MaskRefineDialog : public QDialog {
    Q_OBJECT
  public:
    enum class Output { Selection, LayerMask, NewLayer, NewLayerWithMask };
    MaskRefineDialog(const QImage &source, const QImage &mask, int depth, QWidget *parent = nullptr);
    QImage refinedMask() const;
    QImage outputImage() const;
    Output outputMode() const;

  private:
    QImage m_source, m_original, m_refined;
    int m_depth;
    class Preview;
    Preview *m_preview;
    void updatePreview();
};
} // namespace serika
