#pragma once
#include <QImage>
#include <memory>

namespace serika {
// Local processing hooks: model-backed implementations can replace these without a network service.
class ISubjectSelector {
  public:
    virtual ~ISubjectSelector() = default;
    virtual QImage select(const QImage &image) const = 0;
};
class ColorModelSubjectSelector final : public ISubjectSelector {
  public:
    QImage select(const QImage &image) const override;
};
using LocalSubjectSelector = ColorModelSubjectSelector;
void registerSubjectSelector(std::shared_ptr<ISubjectSelector> selector);
QImage selectSubjectLocally(const QImage &image);
QImage synthesizePatches(const QImage &source, const QImage &mask);
} // namespace serika
