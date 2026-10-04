#include "Snip.h"

#include <atomic>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>

namespace xqt::snip {

namespace {
std::atomic<Shape> shape{Shape::None};
}

void arm(Shape s) { shape = s; }
void disarm() { shape = Shape::None; }
Shape armed() { return shape; }

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
