#pragma once
#include <QJsonObject>
#include <QWidget>
class QComboBox;
class QSpinBox;
namespace serika {
class CurveGraph;
class CurvesEditor final : public QWidget {
    Q_OBJECT
  public:
    explicit CurvesEditor(QWidget *parent = nullptr);
    void setParameters(const QJsonObject &parameters);
    QJsonObject parameters() const;
    void setSource(const QImage &source);
  signals:
    void parametersChanged();

  private:
    CurveGraph *m_graph;
    QComboBox *m_channel;
    QSpinBox *m_input;
    QSpinBox *m_output;
    QJsonObject m_parameters;
    void storeCurve();
    void updateFields();
};
} // namespace serika
