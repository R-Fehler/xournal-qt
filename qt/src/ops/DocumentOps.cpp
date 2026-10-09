#include "DocumentOps.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <shared_mutex>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "control/layer/LayerController.h"
#include "control/pagetype/PageTypeHandler.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/ElementGroups.h"
#include "session/ElementTimes.h"
#include "undo/DeleteUndoAction.h"
#include "undo/InsertLayerUndoAction.h"
#include "undo/InsertUndoAction.h"
#include "undo/LayerRenameUndoAction.h"
#include "undo/PageBackgroundChangedUndoAction.h"
#include "undo/UndoAction.h"
#include "undo/UndoRedoHandler.h"

#include "MdBox.h"
#include "Operations.h"
#include "Shapes.h"

namespace xqt::ops {

namespace {
constexpr int MAX_SHAPES = 20000;  ///< shapes of one insert

QString dataKey(const Principal& p) { return p.id.isEmpty() ? QStringLiteral("user") : p.id; }

/// The principal's value in an element's data (invalid: none)
QVariant dataOf(const Element& e, const Principal& p) {
    if (e.getData().empty()) {
        return {};
    }
    const QJsonObject all = QJsonDocument::fromJson(QByteArray::fromStdString(e.getData())).object();
    const QJsonValue v = all.value(dataKey(p));
    return v.isUndefined() ? QVariant() : v.toVariant();
}

/// The element's data with the principal's value set (null: removed)
std::string withData(const std::string& data, const Principal& p, const QVariant& value) {
    QJsonObject all = QJsonDocument::fromJson(QByteArray::fromStdString(data)).object();
    if (!value.isValid() || value.isNull()) {
        all.remove(dataKey(p));
    } else {
        all.insert(dataKey(p), QJsonValue::fromVariant(value));
    }
    return all.isEmpty() ? std::string() : QJsonDocument(all).toJson(QJsonDocument::Compact).toStdString();
}

const char* typeName(const Element& e) {
    switch (e.getType()) {
        case ELEMENT_STROKE:
            return "stroke";
        case ELEMENT_TEXT:
            return "text";
        case ELEMENT_IMAGE:
            return "image";
        case ELEMENT_TEXIMAGE:
            return "teximage";
        case ELEMENT_LINK:
            return "link";
    }
    return "element";
}

/// One undo step: an element's plugin data changed
class DataUndoAction final: public UndoAction {
public:
    DataUndoAction(PageRef page, Element* e, std::string before, std::string after):
            UndoAction("DataUndoAction"), element(e), before(std::move(before)), after(std::move(after)) {
        this->page = std::move(page);
    }
    bool undo(Control*) override {
        element->setData(before);
        return true;
    }
    bool redo(Control*) override {
        element->setData(after);
        return true;
    }
    std::string getText() override { return "Element data"; }
    std::vector<PageRef> getPages() override { return {}; }  // (no picture changes)

private:
    Element* element;
    std::string before;
    std::string after;
};

PageRef pageAt(DocumentSession& s, size_t index) {
    std::shared_lock lock(*s.getDocument());
    return s.getDocument()->getPage(index);
}

/// The layer `args.layer` of a page (default: the selected one); the caller holds the lock
Layer* layerOf(XojPage& page, const QVariantMap& args, Layer::Index* index = nullptr) {
    auto layers = page.getLayers();
    Layer::Index i = 0;
    if (args.contains("layer") && !args.value("layer").isNull()) {
        const int l = integer(args, "layer");
        if (l < 0 || static_cast<size_t>(l) >= layers.size()) {
            throw Error(Error::Kind::Invalid, QStringLiteral("no layer %1").arg(l));
        }
        i = static_cast<Layer::Index>(l);
    } else {
        const auto selected = page.getSelectedLayerId();
        i = selected > 0 ? selected - 1 : 0;
    }
    if (index) {
        *index = i;
    }
    return layers[i];
}

void changed(DocumentSession& s, const PageRef& page, size_t index) {
    page->firePageChanged();
    s.firePageChanged(index);
}

QVariantMap elementInfo(Context& c, const PageRef& page, Layer* layer, size_t layerIndex, const Element& e,
                        bool withData) {
    const auto box = e.getBoundingBox();
    QVariantMap m{{"ref", c.refs.add(page, layer, &e)},
                  {"type", QString::fromLatin1(typeName(e))},
                  {"layer", static_cast<int>(layerIndex)},
                  {"group", static_cast<int>(e.getGroup())},
                  {"x", box.x},
                  {"y", box.y},
                  {"width", box.width},
                  {"height", box.height},
                  {"color", colorName(e.getColor())}};
    if (withData) {
        if (const QVariant d = dataOf(e, c.principal); d.isValid()) {
            m.insert("data", d);
        }
    }
    return m;
}

// --- reading --------------------------------------------------------------------------------------------------------

QVariant documentRead(Context& c, const QVariantMap&) {
    DocumentSession& s = c.readable();
    std::shared_lock lock(*s.getDocument());
    return QVariantMap{{"pageCount", static_cast<int>(s.getDocument()->getPageCount())},
                       {"currentPage", static_cast<int>(s.getCurrentPageNo())},
                       {"readOnly", s.isReadOnly() || s.isReplaying()},
                       {"file", QString::fromStdString(s.getDocument()->getFilepath().u8string().empty()
                                                               ? std::string()
                                                               : s.getDocument()->getFilepath().filename().string())}};
}

QVariant pageRead(Context& c, const QVariantMap& args) {
    DocumentSession& s = c.readable();
    const size_t index = pageIndex(c, args);
    const PageRef page = pageAt(s, index);
    std::shared_lock lock(*s.getDocument());
    QVariantList layers;
    for (const Layer* l: page->getLayersView()) {
        layers << QVariantMap{{"name", QString::fromStdString(l->getName())},
                              {"visible", l->isVisible()},
                              {"elements", static_cast<int>(l->getElementsView().size())},
                              {"markdown", md::isMarkdownLayer(*l)}};
    }
    const auto selected = page->getSelectedLayerId();
    const PageType type = page->getBackgroundType();
    return QVariantMap{
            {"index", static_cast<int>(index)},
            {"width", page->getWidth()},
            {"height", page->getHeight()},
            {"background",
             QVariantMap{{"type", QString::fromStdString(PageTypeHandler::getStringForPageTypeFormat(type.format))},
                         {"color", colorName(page->getBackgroundColor())}}},
            {"layers", layers},
            {"selectedLayer", static_cast<int>(selected > 0 ? selected - 1 : 0)}};
}

QVariant elementList(Context& c, const QVariantMap& args) {
    DocumentSession& s = c.readable();
    const size_t index = pageIndex(c, args);
    const PageRef page = pageAt(s, index);
    const bool withData = args.value("withData", true).toBool();
    const bool onlyWithData = args.value("onlyWithData", false).toBool();
    std::shared_lock lock(*s.getDocument());
    QVariantList out;
    auto layers = page->getLayers();
    for (size_t li = 0; li < layers.size(); ++li) {
        if (args.contains("layer") && !args.value("layer").isNull() && integer(args, "layer") != static_cast<int>(li)) {
            continue;
        }
        for (const Element* e: layers[li]->getElementsView()) {
            if (onlyWithData && !dataOf(*e, c.principal).isValid()) {
                continue;
            }
            out << elementInfo(c, page, layers[li], li, *e, withData);
        }
    }
    return out;
}

// --- elements -----------------------------------------------------------------------------------------------------

QVariant elementInsert(Context& c, const QVariantMap& args) {
    DocumentSession& s = c.writable();
    const size_t index = pageIndex(c, args);
    const QVariantList shapes = args.value("shapes").toList();
    if (shapes.isEmpty()) {
        throw Error(Error::Kind::Invalid, QStringLiteral("\"shapes\" is empty"));
    }
    if (shapes.size() > MAX_SHAPES) {
        throw Error(Error::Kind::Invalid, QStringLiteral("too many shapes at once"));
    }
    // Every part checked and made before anything changes
    for (const QVariant& v: shapes) {
        c.check(insertOperationOf(v.toMap()), QStringLiteral("edit"));
    }
    std::vector<ElementPtr> ink;
    std::vector<ElementPtr> boxes;
    std::vector<int> order;  // (per shape: its place among ink (>= 0) or boxes (< 0, -1 - i))
    for (const QVariant& v: shapes) {
        MadeElement made = makeElement(v.toMap());
        if (made.markdown) {
            order.push_back(-1 - static_cast<int>(boxes.size()));
            boxes.push_back(std::move(made.element));
        } else {
            order.push_back(static_cast<int>(ink.size()));
            ink.push_back(std::move(made.element));
        }
    }
    const PageRef page = pageAt(s, index);
    Document* doc = s.getDocument();
    Layer* inkLayer = nullptr;
    Layer::Index inkIndex = 0;
    {
        std::shared_lock lock(*doc);
        inkLayer = layerOf(*page, args, &inkIndex);
    }
    Layer* boxLayer = nullptr;
    if (!boxes.empty()) {
        std::shared_lock lock(*doc);
        boxLayer = md::markdownLayer(page);
    }
    if (!boxes.empty() && !boxLayer) {
        // The page's Markdown layer (made at the bottom, an undo step of the transaction; the selected layer stays)
        Layer::Index selected = 0;
        {
            std::shared_lock lock(*doc);
            selected = page->getSelectedLayerId();
        }
        boxLayer = new Layer();
        boxLayer->setName(std::string(xoj::markdown::LAYER_NAME));
        s.getLayerController()->insertLayer(page, boxLayer, 0);
        s.getUndoRedoHandler()->addUndoAction(
                std::make_unique<InsertLayerUndoAction>(s.getLayerController(), page, boxLayer, 0));
        std::unique_lock lock(*doc);
        page->setSelectedLayerId(selected > 0 ? selected + 1 : 0);
        if (inkLayer == boxLayer) {
            inkLayer = page->getLayers()[inkIndex + 1];
        }
    }
    const bool grouped = args.value("group").toBool();
    const QVariant data = args.value("data");
    std::vector<const Element*> inkRaw, boxRaw;
    {
        std::unique_lock lock(*doc);
        for (auto* list: {&ink, &boxes}) {
            if (list->empty()) {
                continue;
            }
            if (grouped && list->size() > 1) {
                const groups::Id g = groups::fresh(*doc);
                for (auto& e: *list) {
                    e->setGroup(g);
                }
            }
            if (data.isValid() && !data.isNull()) {
                list->front()->setData(withData({}, c.principal, data));
            }
        }
        for (auto& e: ink) {
            timeline::stampNew(*e);
            inkRaw.push_back(e.get());
            inkLayer->addElement(std::move(e));
        }
        for (auto& e: boxes) {
            timeline::stampNew(*e);
            boxRaw.push_back(e.get());
            boxLayer->addElement(std::move(e));
        }
    }
    if (!inkRaw.empty()) {
        s.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertsUndoAction>(page, inkLayer, inkRaw));
    }
    if (!boxRaw.empty()) {
        s.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertsUndoAction>(page, boxLayer, boxRaw));
    }
    changed(s, page, index);
    QVariantList refs;
    for (int o: order) {
        refs << (o >= 0 ? c.refs.add(page, inkLayer, inkRaw[static_cast<size_t>(o)])
                        : c.refs.add(page, boxLayer, boxRaw[static_cast<size_t>(-1 - o)]));
    }
    return refs;
}

QVariant elementDelete(Context& c, const QVariantMap& args) {
    DocumentSession& s = c.writable();
    c.check(QStringLiteral("element.delete"), QStringLiteral("edit"));
    const QVariantList refs = args.value("refs").toList();
    const bool withGroups = args.value("withGroups").toBool();
    Document* doc = s.getDocument();
    // Per page and layer: the elements (groups completed)
    std::map<std::pair<size_t, Layer*>, std::pair<PageRef, std::vector<Element*>>> byLayer;
    {
        std::shared_lock lock(*doc);
        for (const QVariant& r: refs) {
            const auto t = c.refs.resolve(r.toString(), s);
            auto& slot = byLayer[{t.pageIndex, t.layer}];
            slot.first = t.page;
            auto* e = const_cast<Element*>(t.element);
            if (std::find(slot.second.begin(), slot.second.end(), e) == slot.second.end()) {
                slot.second.push_back(e);
            }
        }
        if (withGroups) {
            for (auto& [key, slot]: byLayer) {
                slot.second = groups::withMembers(*key.second, slot.second);
            }
        }
    }
    int count = 0;
    for (auto& [key, slot]: byLayer) {
        auto undo = std::make_unique<DeleteUndoAction>(slot.first, false);
        {
            std::unique_lock lock(*doc);
            for (Element* e: slot.second) {
                const auto pos = key.second->indexOf(e);
                if (pos == Element::InvalidIndex) {
                    continue;
                }
                auto removed = key.second->removeElement(e);
                undo->addElement(key.second, std::move(removed.e), pos);
                ++count;
            }
        }
        s.getUndoRedoHandler()->addUndoAction(std::move(undo));
        changed(s, slot.first, key.first);
    }
    return count;
}

QVariant elementData(Context& c, const QVariantMap& args) {
    DocumentSession& s = c.writable();
    std::unique_ptr<DataUndoAction> undo;
    {
        std::unique_lock lock(*s.getDocument());
        const auto t = c.refs.resolve(string(args, "ref"), s);
        auto* e = const_cast<Element*>(t.element);
        const std::string before = e->getData();
        const std::string after = withData(before, c.principal, args.value("value"));
        if (after == before) {
            return false;
        }
        e->setData(after);
        undo = std::make_unique<DataUndoAction>(t.page, e, before, after);
    }
    s.getUndoRedoHandler()->addUndoAction(std::move(undo));
    return true;
}

// --- layers -------------------------------------------------------------------------------------------------------

QVariant layerAdd(Context& c, const QVariantMap& args) {
    DocumentSession& s = c.writable();
    const size_t index = pageIndex(c, args);
    const PageRef page = pageAt(s, index);
    Layer::Index pos = 0;
    {
        std::shared_lock lock(*s.getDocument());
        pos = args.contains("above") ? static_cast<Layer::Index>(std::clamp<int>(
                                               integer(args, "above") + 1, 0, static_cast<int>(page->getLayerCount())))
                                     : page->getLayerCount();
    }
    auto* layer = new Layer();
    if (const QString name = args.value("name").toString(); !name.isEmpty()) {
        layer->setName(name.toStdString());
    }
    s.getLayerController()->insertLayer(page, layer, pos);
    s.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertLayerUndoAction>(s.getLayerController(), page, layer, pos));
    {
        std::unique_lock lock(*s.getDocument());
        page->setSelectedLayerId(pos + 1);  // (what is inserted next goes into it)
    }
    changed(s, page, index);
    return static_cast<int>(pos);
}

QVariant layerRename(Context& c, const QVariantMap& args) {
    DocumentSession& s = c.writable();
    const size_t index = pageIndex(c, args);
    const PageRef page = pageAt(s, index);
    const QString name = string(args, "name");
    Layer* layer = nullptr;
    std::string before;
    {
        std::unique_lock lock(*s.getDocument());
        layer = layerOf(*page, args);
        before = layer->getName();
        layer->setName(name.toStdString());
    }
    s.getUndoRedoHandler()->addUndoAction(
            std::make_unique<LayerRenameUndoAction>(s.getLayerController(), layer, name.toStdString(), before));
    s.getLayerController()->fireRebuildLayerMenu();
    return true;
}

/// Layer visibility has no undo step upstream (a view setting there); here it is one, so a command undoes whole
class LayerVisibleUndoAction final: public UndoAction {
public:
    LayerVisibleUndoAction(PageRef page, Layer* layer, bool visible):
            UndoAction("LayerVisibleUndoAction"), layer(layer), visible(visible) {
        this->page = std::move(page);
    }
    bool undo(Control*) override {
        layer->setVisible(!visible);
        page->firePageChanged();
        return true;
    }
    bool redo(Control*) override {
        layer->setVisible(visible);
        page->firePageChanged();
        return true;
    }
    std::string getText() override { return "Layer visibility"; }

private:
    Layer* layer;
    bool visible;
};

QVariant layerVisible(Context& c, const QVariantMap& args) {
    DocumentSession& s = c.writable();
    const size_t index = pageIndex(c, args);
    const PageRef page = pageAt(s, index);
    const bool visible = args.value("visible", true).toBool();
    Layer* layer = nullptr;
    {
        std::unique_lock lock(*s.getDocument());
        layer = layerOf(*page, args);
        if (layer->isVisible() == visible) {
            return false;
        }
        layer->setVisible(visible);
    }
    s.getUndoRedoHandler()->addUndoAction(std::make_unique<LayerVisibleUndoAction>(page, layer, visible));
    s.getLayerController()->fireRebuildLayerMenu();
    changed(s, page, index);
    return true;
}

QVariant layerSelect(Context& c, const QVariantMap& args) {
    DocumentSession& s = c.writable();
    const size_t index = pageIndex(c, args);
    const PageRef page = pageAt(s, index);
    {
        std::unique_lock lock(*s.getDocument());
        Layer::Index i = 0;
        layerOf(*page, args, &i);
        page->setSelectedLayerId(i + 1);
    }
    s.getLayerController()->fireRebuildLayerMenu();
    return true;
}

// --- pages --------------------------------------------------------------------------------------------------------

PageType pageTypeOf(const QVariantMap& args) {
    const QString name = string(args, "type");
    const PageTypeFormat f = PageTypeHandler::getPageTypeFormatForString(name.toStdString());
    if (f == PageTypeFormat::Pdf || f == PageTypeFormat::Image ||
        PageTypeHandler::getStringForPageTypeFormat(f) != name.toStdString()) {
        throw Error(Error::Kind::Invalid,
                    QStringLiteral("no background \"%1\" (plain, lined, ruled, graph, dotted, staves, isodotted, "
                                   "isograph)")
                            .arg(name));
    }
    return PageType(f);
}

QVariant pageInsert(Context& c, const QVariantMap& args) {
    DocumentSession& s = c.writable();
    Document* doc = s.getDocument();
    size_t count = 0;
    size_t at = 0;
    PageRef like;
    {
        std::shared_lock lock(*doc);
        const int n = static_cast<int>(doc->getPageCount());
        const int a = integer(args, "at", static_cast<int>(s.getCurrentPageNo()) + 1);
        const int k = integer(args, "count", 1);
        if (a < 0 || a > n || k < 1 || k > 1000) {
            throw Error(Error::Kind::Invalid, QStringLiteral("pages can be inserted at 0 to %1, 1 to 1000").arg(n));
        }
        at = static_cast<size_t>(a);
        count = static_cast<size_t>(k);
        like = doc->getPage(at > 0 ? at - 1 : 0);
    }
    const std::optional<PageType> type =
            args.contains("background") ? std::optional(pageTypeOf({{"type", args.value("background")}})) : std::nullopt;
    std::vector<PageRef> pages;
    for (size_t i = 0; i < count; ++i) {
        auto p = std::make_shared<XojPage>(like->getWidth(), like->getHeight());
        const PageType t = like->getBackgroundType();
        p->setBackgroundType(type ? *type : (t.isPdfPage() || t.isImagePage() ? PageType(PageTypeFormat::Plain) : t));
        p->setBackgroundColor(like->getBackgroundColor());
        pages.push_back(p);
    }
    s.insertPages(pages, at);
    return static_cast<int>(at);
}

QVariant pageDelete(Context& c, const QVariantMap& args) {
    DocumentSession& s = c.writable();
    std::vector<size_t> pages;
    for (const QVariant& v: args.value("pages").toList()) {
        pages.push_back(static_cast<size_t>(std::max(0, v.toInt())));
    }
    if (pages.empty()) {
        throw Error(Error::Kind::Invalid, QStringLiteral("\"pages\" is empty"));
    }
    return s.deletePages(pages);
}

QVariant backgroundSet(Context& c, const QVariantMap& args) {
    DocumentSession& s = c.writable();
    const PageType type = pageTypeOf(args);
    std::optional<Color> color;
    if (args.contains("color")) {
        color = parseColor(args.value("color").toString());
        if (!color) {
            throw Error(Error::Kind::Invalid, QStringLiteral("\"color\" is no color (#rrggbb)"));
        }
    }
    Document* doc = s.getDocument();
    std::vector<size_t> indices;
    if (args.contains("pages")) {
        for (const QVariant& v: args.value("pages").toList()) {
            indices.push_back(static_cast<size_t>(std::max(0, v.toInt())));
        }
    } else {
        indices.push_back(pageIndex(c, args));
    }
    std::vector<size_t> done;
    {
        std::unique_lock lock(*doc);
        for (size_t i: indices) {
            if (i >= doc->getPageCount()) {
                throw Error(Error::Kind::Invalid, QStringLiteral("no page %1").arg(i));
            }
        }
        for (size_t i: indices) {
            const PageRef page = doc->getPage(i);
            if (page->getBackgroundType().isPdfPage() || page->getBackgroundType().isImagePage()) {
                continue;  // (a PDF or picture page keeps its background)
            }
            auto undo = std::make_unique<PageBackgroundChangedUndoAction>(
                    page, page->getBackgroundType(), page->getPdfPageNr(), page->getBackgroundImage(),
                    page->getWidth(), page->getHeight());
            page->setBackgroundType(type);
            if (color) {
                page->setBackgroundColor(*color);
            }
            lock.unlock();
            s.getUndoRedoHandler()->addUndoAction(std::move(undo));
            lock.lock();
            done.push_back(i);
        }
    }
    for (size_t i: done) {
        s.firePageChanged(i);
    }
    return static_cast<int>(done.size());
}
}  // namespace

void addDocumentOperations(Operations& ops) {
    ops.add("document.read", "read", false, documentRead);
    ops.add("page.read", "read", false, pageRead);
    ops.add("element.list", "read", false, elementList);
    ops.add("element.insert", "edit", true, elementInsert);
    ops.add("element.delete", "edit", true, elementDelete);
    ops.add("element.data", "edit", true, elementData);
    ops.add("layer.add", "pages", true, layerAdd);
    ops.add("layer.rename", "pages", true, layerRename);
    ops.add("layer.visible", "pages", true, layerVisible);
    ops.add("layer.select", "pages", true, layerSelect);
    ops.add("page.insert", "pages", true, pageInsert);
    ops.add("page.delete", "pages", true, pageDelete);
    ops.add("background.set", "pages", true, backgroundSet);
}

}  // namespace xqt::ops
