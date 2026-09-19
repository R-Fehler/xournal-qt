/*
 * xournal-qt: application controller exposed to QML (single document for now; tabs come next).
 *
 * Owns the shared AppContext, the open DocumentSession and its CanvasView. The tool state is upstream's ToolHandler
 * (shared by all documents): the QML tool bar reads and changes it through this object.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

namespace xqt {
class AppContext;
class CanvasView;
class DocumentSession;
}  // namespace xqt
class Palette;

class AppController: public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* view READ view NOTIFY documentChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(bool hasFilePath READ hasFilePath NOTIFY titleChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoRedoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoRedoChanged)
    Q_PROPERTY(QString tool READ tool NOTIFY toolChanged)
    Q_PROPERTY(QColor color READ color NOTIFY toolChanged)
    Q_PROPERTY(int size READ size NOTIFY toolChanged)
    Q_PROPERTY(QVariantList palette READ palette CONSTANT)
    Q_PROPERTY(int zoomPercent READ zoomPercent NOTIFY zoomChanged)
    Q_PROPERTY(int pageNumber READ pageNumber NOTIFY pageChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY pageChanged)
public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    QObject* view() const;
    QString title() const;
    bool modified() const;
    bool hasFilePath() const;
    bool canUndo() const;
    bool canRedo() const;
    QString tool() const;
    QColor color() const;
    int size() const;
    QVariantList palette() const;
    int zoomPercent() const;
    int pageNumber() const;
    int pageCount() const;

    Q_INVOKABLE void newDocument();
    Q_INVOKABLE bool openFile(const QUrl& url);
    Q_INVOKABLE bool openPath(const QString& path);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl& url);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    /// "pen", "highlighter", "eraser", "hand"
    Q_INVOKABLE void selectTool(const QString& tool);
    Q_INVOKABLE void setColor(const QColor& color);
    /// 0 = very fine ... 4 = very thick (upstream ToolSize)
    Q_INVOKABLE void setSize(int size);
    Q_INVOKABLE void fitWidth();
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void addPageAfterCurrent();
    Q_INVOKABLE QUrl iconUrl(const QString& name) const;
    /// Folder for the Open dialog: the current document's folder, else the last folder a file was opened from.
    Q_INVOKABLE QUrl openFolder() const;
    /// Suggestion for "Save as" (port of upstream Control::saveImpl): for an annotated PDF the .xopp next to the
    /// PDF ("lecture.pdf" -> "lecture.xopp"), else the document's own path or the default name in the last folder.
    Q_INVOKABLE QUrl suggestedSaveFile() const;
    /// Call before quitting: writes settings.
    Q_INVOKABLE void shutdown();

Q_SIGNALS:
    void documentChanged();
    void titleChanged();
    void modifiedChanged();
    void undoRedoChanged();
    void toolChanged();
    void zoomChanged();
    void pageChanged();
    /// Messages from the core (XojMsgBox) and file errors, shown by QML.
    void message(const QString& title, const QString& text, bool error);

private:
    void setSession(std::unique_ptr<xqt::DocumentSession> session);

    std::unique_ptr<xqt::AppContext> app;
    std::unique_ptr<Palette> colors;
    std::unique_ptr<xqt::DocumentSession> session;
    std::unique_ptr<xqt::CanvasView> canvas;
};
