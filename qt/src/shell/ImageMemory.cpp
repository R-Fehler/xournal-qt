#include "ImageMemory.h"

#include <algorithm>
#include <atomic>

#include "CanvasMemory.h"
#include "PageSketches.h"
#include "Thumbnails.h"

namespace xqt {

namespace {
std::atomic<qint64> previewBytes{ImageMemory::DEFAULT_PREVIEW_MB * ImageMemory::MB};
}

void ImageMemory::setPreviewMemory(qint64 bytes) {
    bytes = std::max<qint64>(0, bytes);
    previewBytes = bytes;
    ThumbnailProvider::setCacheLimit(thumbnailShare(bytes));
    PageSketches::instance().setBudget(sketchShare(bytes));
}

qint64 ImageMemory::previewMemory() { return previewBytes.load(); }

qint64 ImageMemory::standInBudget() { return CanvasMemory::instance().standInBudget(); }

}  // namespace xqt
