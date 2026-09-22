#include "LayersModel.h"

#include <shared_mutex>

#include "control/layer/LayerController.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"

namespace xqt {

LayersModel::LayersModel(QObject* parent): QAbstractListModel(parent) {}

LayersModel::~LayersModel() { setSession(nullptr); }

void LayersModel::setSession(DocumentSession* s) {
    if (s == session) {
        return;
    }
    LayerCtrlListener::unregisterListener();
    DocumentListener::unregisterListener();
    session = s;
    if (session) {
        DocumentListener::registerListener(session);
        LayerCtrlListener::registerListener(session->getLayerController());
    }
    rebuild();
}

void LayersModel::rebuild() {
    std::vector<Entry> next;
    if (session) {
        Document* doc = session->getDocument();
        std::shared_lock lock(*doc);
        if (const PageRef page = doc->getPage(std::min(session->getCurrentPageNo(), doc->getPageCount() - 1))) {
            const auto layers = page->getLayersView();
            // Top first, like Xournal++ shows them; the background is the last row.
            for (size_t i = layers.size(); i > 0; --i) {
                const Layer* layer = layers[i - 1];
                Entry e;
                e.id = i;  // upstream: 1 is the first layer above the background
                e.name = layer->hasName() ? QString::fromStdString(layer->getName())
                                          : tr("Layer %1").arg(static_cast<int>(i));
                e.visible = layer->isVisible();
                e.elements = static_cast<int>(layer->getElementsView().size());
                next.push_back(std::move(e));
            }
            Entry background;
            background.id = 0;
            background.name = tr("Background");
            background.visible = true;
            next.push_back(std::move(background));
        }
    }
    // Only when the layers really changed: resetting the model makes QML build all its delegates again (7 ms), and
    // this is called twice for every change of the current page (scrolling through a document).
    if (next != entries) {
        if (next.size() == entries.size()) {
            entries = std::move(next);
            Q_EMIT dataChanged(index(0), index(rowCount() - 1));
        } else {
            beginResetModel();
            entries = std::move(next);
            endResetModel();
        }
    }
    Q_EMIT changed();  // the selected layer may be another one now
}

int LayersModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries.size());
}

size_t LayersModel::idOf(int row) const {
    return row >= 0 && row < rowCount() ? entries[static_cast<size_t>(row)].id : 0;
}

int LayersModel::currentRow() const {
    if (!session) {
        return -1;
    }
    const size_t current = session->getLayerController()->getCurrentLayerId();
    for (int row = 0; row < rowCount(); ++row) {
        if (entries[static_cast<size_t>(row)].id == current) {
            return row;
        }
    }
    return -1;
}

QVariant LayersModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const Entry& e = entries[static_cast<size_t>(index.row())];
    switch (role) {
        case NameRole:
            return e.name;
        case VisibleRole:
            return e.visible;
        case CurrentRole:
            return index.row() == currentRow();
        case LayerIdRole:
            return static_cast<int>(e.id);
        case IsBackgroundRole:
            return e.id == 0;
        case ElementCountRole:
            return e.elements;
        default:
            return {};
    }
}

QHash<int, QByteArray> LayersModel::roleNames() const {
    return {{NameRole, "name"},
            {VisibleRole, "layerVisible"},
            {CurrentRole, "current"},
            {LayerIdRole, "layerId"},
            {IsBackgroundRole, "isBackground"},
            {ElementCountRole, "elementCount"}};
}

void LayersModel::select(int row) {
    if (session && row >= 0 && row < rowCount()) {
        session->getLayerController()->switchToLay(idOf(row));
    }
}

bool LayersModel::makeCurrent(int row) {
    if (!session || row < 0 || row >= rowCount() || idOf(row) == 0) {
        return false;  // (the background is no layer)
    }
    if (row != currentRow()) {
        session->getLayerController()->switchToLay(idOf(row));
    }
    return true;
}

void LayersModel::setVisible(int row, bool visible) {
    if (session && row >= 0 && row < rowCount()) {
        session->getLayerController()->setLayerVisible(idOf(row), visible);
    }
}

void LayersModel::showAll(bool show) {
    if (session) {
        session->getLayerController()->showOrHideAllLayer(show);
    }
}

void LayersModel::addLayer(bool below) {
    if (session) {
        session->getLayerController()->addNewLayer(below);
    }
}

void LayersModel::duplicate(int row) {
    if (makeCurrent(row)) {
        session->getLayerController()->copyCurrentLayer();
    }
}

void LayersModel::remove(int row) {
    if (makeCurrent(row)) {
        session->getLayerController()->deleteCurrentLayer();
    }
}

void LayersModel::moveUp(int row) {
    if (makeCurrent(row)) {
        session->getLayerController()->moveCurrentLayer(true);
    }
}

void LayersModel::moveDown(int row) {
    if (makeCurrent(row)) {
        session->getLayerController()->moveCurrentLayer(false);
    }
}

void LayersModel::mergeDown(int row) {
    if (makeCurrent(row)) {
        session->getLayerController()->mergeCurrentLayerDown();
    }
}

void LayersModel::rename(int row, const QString& name) {
    if (makeCurrent(row) && !name.trimmed().isEmpty()) {
        session->getLayerController()->setCurrentLayerName(name.trimmed().toStdString());
    }
}

void LayersModel::documentChanged(DocumentChangeType) { rebuild(); }
void LayersModel::pageSelected(size_t) { rebuild(); }
void LayersModel::pageInserted(size_t) { rebuild(); }
void LayersModel::pageDeleted(size_t) { rebuild(); }
void LayersModel::rebuildLayerMenu() { rebuild(); }
void LayersModel::layerVisibilityChanged() { rebuild(); }
void LayersModel::updateSelectedLayer() { rebuild(); }

}  // namespace xqt
