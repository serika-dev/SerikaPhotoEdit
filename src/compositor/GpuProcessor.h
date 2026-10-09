#pragma once
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>

namespace serika {
struct GpuDiagnostics {
    bool compiled = false;
    bool enabled = false;
    bool available = false;
    bool softwareRenderer = false;
    QString vendor;
    QString renderer;
    QString version;
    QString reason;
    QString lastOperation;
    QString lastBackend = "CPU";
    QString lastFallback;
    int maximumTextureSize = 0;
    quint64 completedOperations = 0;
    QString summary() const;
};
struct GpuProcessingResult {
    QImage image;
    bool usedGpu = false;
    QString reason;
};

// The CPU dispatchers use the image only when usedGpu is true; all failure paths retain CPU behavior.
class GpuProcessor final {
  public:
    static GpuProcessor &instance();
    ~GpuProcessor();
    void setEnabled(bool enabled);
    bool enabled() const;
    GpuDiagnostics diagnostics(bool probe = true);
    GpuProcessingResult processAdjustment(const QImage &source, const QString &name,
                                          const QJsonObject &parameters = {});
    GpuProcessingResult processFilter(const QImage &source, const QString &name,
                                      const QJsonObject &parameters = {});
    static QStringList supportedAdjustments();
    static QStringList supportedFilters();
    // Call on the GUI thread before application teardown. A later operation can initialize again.
    void shutdown();

  private:
    GpuProcessor();
    GpuProcessor(const GpuProcessor &) = delete;
    GpuProcessor &operator=(const GpuProcessor &) = delete;
    struct State;
    std::unique_ptr<State> m_state;
    std::atomic_bool m_enabled = false;
    GpuDiagnostics m_diagnostics;
    bool initialize();
    GpuProcessingResult fallback(const QString &operation, const QString &reason);
    GpuProcessingResult process(const QImage &source, const QString &name, const QJsonObject &parameters,
                                bool blur);
};
} // namespace serika
