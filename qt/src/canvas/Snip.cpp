#include "Snip.h"

#include <atomic>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>

#include "render/RegionRender.h"

namespace xqt::snip {

namespace {
std::atomic<Shape> shape{Shape::None};
std::atomic<Purpose> what{Purpose::Picture};
std::atomic<Resolution> sharpness{Resolution::Screen};
}  // namespace

void setResolution(Resolution r) { sharpness = r; }
Resolution resolution() { return sharpness; }
double minDpi(Resolution r) {
    return r == Resolution::VeryHigh ? 600 : r == Resolution::High ? 300 : region::MIN_DPI;
}
double maxPixels(Resolution r) { return r == Resolution::Screen ? region::MAX_PIXELS : HIGH_MAX_PIXELS; }

void arm(Shape s, Purpose p) {
    what = p;
    shape = s;
}
void disarm() {
    shape = Shape::None;
    what = Purpose::Picture;
}
Shape armed() { return shape; }
Purpose purpose() { return what; }

QByteArray encode(const Source& source) {
    QJsonObject o;
    o["title"] = source.title;
    o["link"] = source.link;
    o["page"] = source.page;
    o["area"] = QJsonArray{source.area.x(), source.area.y(), source.area.width(), source.area.height()};
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

std::optional<Source> decode(const QMimeData* mime) {
    if (!mime || !mime->hasFormat(MIME)) {
        return std::nullopt;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(mime->data(MIME));
    if (!doc.isObject()) {
        return std::nullopt;
    }
    const QJsonObject o = doc.object();
    Source s;
    s.title = o["title"].toString();
    s.link = o["link"].toString();
    s.page = o["page"].toInt();
    const QJsonArray a = o["area"].toArray();
    if (a.size() == 4) {
        s.area = QRectF(a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble());
    }
    return s;
}

}  // namespace xqt::snip
