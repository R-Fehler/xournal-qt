/*
 * xournal-qt: minimal GTK compatibility header for compiling the Xournal++ core without GTK.
 * See gdk/gdk.h. Only opaque widget handle types are declared; any GTK call fails to compile.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <gdk/gdk.h>

G_BEGIN_DECLS

typedef struct _GtkWidget GtkWidget;
typedef struct _GtkWindow GtkWindow;
typedef struct _GtkApplication GtkApplication;
typedef struct _GtkTreeModel GtkTreeModel;
typedef struct _GtkTreeStore GtkTreeStore;
typedef struct _GtkFileChooser GtkFileChooser;
typedef struct _GtkPaperSize GtkPaperSize;
typedef struct _GtkAdjustment GtkAdjustment;
typedef struct _GtkDialog GtkDialog;

typedef enum { GTK_MESSAGE_INFO, GTK_MESSAGE_WARNING, GTK_MESSAGE_QUESTION, GTK_MESSAGE_ERROR, GTK_MESSAGE_OTHER } GtkMessageType;

typedef enum {
    GTK_RESPONSE_NONE = -1,
    GTK_RESPONSE_REJECT = -2,
    GTK_RESPONSE_ACCEPT = -3,
    GTK_RESPONSE_DELETE_EVENT = -4,
    GTK_RESPONSE_OK = -5,
    GTK_RESPONSE_CANCEL = -6,
    GTK_RESPONSE_CLOSE = -7,
    GTK_RESPONSE_YES = -8,
    GTK_RESPONSE_NO = -9,
    GTK_RESPONSE_APPLY = -10,
    GTK_RESPONSE_HELP = -11
} GtkResponseType;

G_END_DECLS
