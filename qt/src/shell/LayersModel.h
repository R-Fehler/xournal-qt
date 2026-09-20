/*
 * xournal-qt: the layers of the current page, for the layer panel in the sidebar.
 *
 * Top layer first, the background last (as in Xournal++). Everything goes through the reused upstream
 * LayerController, so adding, removing, moving, merging and renaming are undoable in the same way.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QAbstractListModel>
#include <QPointer>

#include "control/layer/LayerCtrlListener.h"
#include "model/DocumentListener.h"

namespace xqt {

class DocumentSession;

class LayersModel final: public QAbstractListModel, public DocumentListener, public LayerCtrlListener {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
    /// Row of the layer that is drawn on (-1: none)
    Q_PROPERTY(int currentRow READ currentRow NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        VisibleRole,
        CurrentRole,
        /// Upstream layer id: 0 is the background, 1 the first layer
        LayerIdRole,
        IsBackgroundRole,
        ElementCountRole
    };

    explicit LayersModel(QObject* parent = nullptr);
    ~LayersModel() override;

    void setSession(DocumentSession* session);
    bool available() const { return session != nullptr; }
    int currentRow() const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Draw on this layer from now on.
    Q_INVOKABLE void select(int row);
    Q_INVOKABLE void setVisible(int row, bool visible);
    Q_INVOKABLE void showAll(bool show);
    /// A new layer above (or below) the current one, and it becomes the current one.
    Q_INVOKABLE void addLayer(bool below = false);
    Q_INVOKABLE void duplicate(int row);
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void moveUp(int row);
    Q_INVOKABLE void moveDown(int row);
    /// Put the layer into the one below it.
    Q_INVOKABLE void mergeDown(int row);
    Q_INVOKABLE void rename(int row, const QString& name);

Q_SIGNALS:
    void changed();

private:
    // DocumentListener
    void documentChanged(DocumentChangeType type) override;
    void pageSelected(size_t page) override;
    void pageInserted(size_t page) override;
    void pageDeleted(size_t page) override;
    // LayerCtrlListener
    void rebuildLayerMenu() override;
    void layerVisibilityChanged() override;
    void updateSelectedLayer() override;

    void rebuild();
    /// Upstream layer id of a row (rows are top first).
    size_t idOf(int row) const;
    /// Makes that layer the current one first: the controller works on the current layer.
    bool makeCurrent(int row);

    struct Entry {
        QString name;
        bool visible = true;
        size_t id = 0;  ///< 0: background
        int elements = 0;
    };
    DocumentSession* session = nullptr;
    std::vector<Entry> entries;
};

}  // namespace xqt
