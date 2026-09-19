/*
 * xournal-qt: replacement for upstream util/XojMsgBox.h (shadows it in the Qt build).
 *
 * Same static API as upstream so that reused core code compiles unchanged, but instead of GTK dialogs the
 * requests are forwarded to a pluggable MessageSink: the Qt application shows QML dialogs, the CLI prints
 * to stderr. The GtkWindow* parameters are ignored.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtk/gtk.h>  // xournal-qt shim: GtkWindow, GtkMessageType

#include "util/move_only_function.h"

#include "filesystem.h"

namespace xoj::compat {
enum class MessageKind { Info, Warning, Question, Error, Other };

struct MessageButton {
    std::string label;
    int response;
};

struct MessageRequest {
    MessageKind kind = MessageKind::Info;
    std::string title;          ///< may be empty
    std::string text;           ///< plain text or Pango markup (see `markup`)
    bool markup = false;
    std::vector<MessageButton> buttons;  ///< empty: plain notification with an OK button
    std::string sourcePlugin;   ///< non-empty for plugin messages
};

/// Presents `request`; must eventually call `onResponse` with the chosen button's response id (for plain
/// notifications the value is ignored). May be called from any thread; implementations must marshal to the UI thread.
using MessageSink = xoj::util::move_only_function<void(MessageRequest request,
                                                       xoj::util::move_only_function<void(int)> onResponse)>;

/// Installs the sink. The default sink logs to stderr and answers questions with GTK_RESPONSE_CANCEL.
void setMessageSink(MessageSink sink);
/// Called by XojMsgBox::openURL / showHelp. Default: g_app_info_launch_default_for_uri.
void setUrlOpener(xoj::util::move_only_function<void(const char* url)> opener);
}  // namespace xoj::compat

class XojMsgBox final {
public:
    enum CallbackPolicy { IMMEDIATE, POSTPONED };

    struct Button {
        Button(std::string l, int r): label(std::move(l)), response(r) {}
        std::string label;
        int response;
    };

    static void setDefaultWindow(GtkWindow* win);

    static void askQuestion(GtkWindow* win, const std::string& maintext, const std::string& secondarytext,
                            const std::vector<Button>& buttons, xoj::util::move_only_function<void(int)> callback);
    static void askQuestionWithMarkup(GtkWindow* win, std::string_view maintext, const std::string& secondarytext,
                                      const std::vector<Button>& buttons,
                                      xoj::util::move_only_function<void(int)> callback);

    static void showMarkupMessageToUser(GtkWindow* win, const std::string_view& markupTitle, const std::string& msg,
                                        GtkMessageType type);
    static void showMessageToUser(GtkWindow* win, const std::string& msg, GtkMessageType type);
    static void showMessageToUser(GtkWindow* win, const std::string& title, const std::string& msg,
                                  GtkMessageType type);
    static void showErrorToUser(GtkWindow* win, const std::string& msg);

    [[noreturn]] static void showErrorAndQuit(std::string& msg, int exitCode);

    static void showPluginMessage(const std::string& pluginName, const std::string& msg, bool error = false);

    static void showHelp(GtkWindow* win);
    static void openURL(GtkWindow* win, const char* url);

    static void replaceFileQuestion(GtkWindow* win, fs::path file,
                                    xoj::util::move_only_function<void(const fs::path&)> writeToFile);
};
