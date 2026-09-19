/*
 * xournal-qt: shadow of upstream control/Control.h.
 *
 * Upstream's Control is the GTK application "god object". Reused upstream code (undo actions, layer controller,
 * tools, input handlers) only calls a small part of its API. In the Qt build, `Control` is therefore an abstract
 * *per-document session* interface with exactly those methods (same names and signatures). The Qt application
 * implements one per tab (see qt/src/session). Methods are added here when more upstream code is enabled.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>  // for size_t

#include <gtk/gtk.h>  // xournal-qt shim: GtkWindow (always nullptr in the Qt build)

#include "model/DocumentHandler.h"  // for DocumentHandler
#include "model/PageRef.h"          // for PageRef

class ActionDatabase;
class AudioController;
class Document;
class LayerController;
class MainWindow;
class PageTypeHandler;
class ScrollHandler;
class Settings;
class ToolHandler;
class UndoRedoHandler;
class XournalppCursor;
class ZoomControl;

class Control: public DocumentHandler {
public:
    virtual ~Control() = default;

    virtual Settings* getSettings() const = 0;
    virtual ToolHandler* getToolHandler() const = 0;
    virtual ZoomControl* getZoomControl() const = 0;
    virtual Document* getDocument() const = 0;
    virtual UndoRedoHandler* getUndoRedoHandler() const = 0;
    virtual MainWindow* getWindow() const = 0;
    /// Upstream: the GTK main window, used as dialog parent. Always nullptr in the Qt build.
    virtual GtkWindow* getGtkWindow() const { return nullptr; }
    virtual ScrollHandler* getScrollHandler() const = 0;
    virtual PageRef getCurrentPage() = 0;
    virtual size_t getCurrentPageNo() const = 0;
    virtual XournalppCursor* getCursor() const = 0;
    virtual AudioController* getAudioController() const { return nullptr; }
    virtual PageTypeHandler* getPageTypes() const = 0;
    virtual LayerController* getLayerController() const = 0;
    virtual ActionDatabase* getActionDatabase() const = 0;

    /// End text editing and clear the selection (called before most document modifications).
    virtual void clearSelectionEndText() = 0;
    virtual void setCopyCutEnabled(bool enabled) = 0;
    virtual void insertNewPage(size_t position, bool automatedInsertion = false) = 0;
    virtual void insertPage(const PageRef& page, size_t position, bool shouldScrollToPage = true) = 0;
    virtual void updateBackgroundSizeButton() {}
};
