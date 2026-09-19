/*
 * xournal-qt: implementation of the XojMsgBox replacement (see compat/include/util/XojMsgBox.h).
 *
 * @license GNU GPLv2 or later
 */
#include "util/XojMsgBox.h"

#include <cstdlib>
#include <mutex>

#include <gio/gio.h>
#include <glib.h>

#include "util/PathUtil.h"
#include "util/i18n.h"

namespace {
std::mutex sinkMutex;

xoj::compat::MessageSink& sinkStorage() {
    static xoj::compat::MessageSink sink;
    return sink;
}

xoj::util::move_only_function<void(const char*)>& urlOpenerStorage() {
    static xoj::util::move_only_function<void(const char*)> opener;
    return opener;
}

xoj::compat::MessageKind kindOf(GtkMessageType type) {
    switch (type) {
        case GTK_MESSAGE_INFO:
            return xoj::compat::MessageKind::Info;
        case GTK_MESSAGE_WARNING:
            return xoj::compat::MessageKind::Warning;
        case GTK_MESSAGE_QUESTION:
            return xoj::compat::MessageKind::Question;
        case GTK_MESSAGE_ERROR:
            return xoj::compat::MessageKind::Error;
        default:
            return xoj::compat::MessageKind::Other;
    }
}

void dispatch(xoj::compat::MessageRequest request, xoj::util::move_only_function<void(int)> onResponse) {
    std::unique_lock lock(sinkMutex);
    auto& sink = sinkStorage();
    if (sink) {
        lock.unlock();
        sink(std::move(request), std::move(onResponse));
        return;
    }
    lock.unlock();
    const char* kind = request.kind == xoj::compat::MessageKind::Error     ? "error"
                       : request.kind == xoj::compat::MessageKind::Warning ? "warning"
                                                                           : "message";
    if (!request.sourcePlugin.empty()) {
        g_message("[plugin %s] %s: %s", request.sourcePlugin.c_str(), kind, request.text.c_str());
    } else if (request.title.empty()) {
        g_message("%s: %s", kind, request.text.c_str());
    } else {
        g_message("%s: %s — %s", kind, request.title.c_str(), request.text.c_str());
    }
    if (onResponse) {
        onResponse(GTK_RESPONSE_CANCEL);
    }
}

std::vector<xoj::compat::MessageButton> toButtons(const std::vector<XojMsgBox::Button>& buttons) {
    std::vector<xoj::compat::MessageButton> out;
    out.reserve(buttons.size());
    for (const auto& b: buttons) {
        out.push_back({b.label, b.response});
    }
    return out;
}
}  // namespace

namespace xoj::compat {
void setMessageSink(MessageSink sink) {
    std::lock_guard lock(sinkMutex);
    sinkStorage() = std::move(sink);
}

void setUrlOpener(xoj::util::move_only_function<void(const char*)> opener) {
    std::lock_guard lock(sinkMutex);
    urlOpenerStorage() = std::move(opener);
}
}  // namespace xoj::compat

void XojMsgBox::setDefaultWindow(GtkWindow*) {}

void XojMsgBox::askQuestion(GtkWindow*, const std::string& maintext, const std::string& secondarytext,
                            const std::vector<Button>& buttons, xoj::util::move_only_function<void(int)> callback) {
    xoj::compat::MessageRequest r;
    r.kind = xoj::compat::MessageKind::Question;
    r.title = maintext;
    r.text = secondarytext;
    r.buttons = toButtons(buttons);
    dispatch(std::move(r), std::move(callback));
}

void XojMsgBox::askQuestionWithMarkup(GtkWindow*, std::string_view maintext, const std::string& secondarytext,
                                      const std::vector<Button>& buttons,
                                      xoj::util::move_only_function<void(int)> callback) {
    xoj::compat::MessageRequest r;
    r.kind = xoj::compat::MessageKind::Question;
    r.title = std::string(maintext);
    r.text = secondarytext;
    r.markup = true;
    r.buttons = toButtons(buttons);
    dispatch(std::move(r), std::move(callback));
}

void XojMsgBox::showMarkupMessageToUser(GtkWindow*, const std::string_view& markupTitle, const std::string& msg,
                                        GtkMessageType type) {
    xoj::compat::MessageRequest r;
    r.kind = kindOf(type);
    r.title = std::string(markupTitle);
    r.text = msg;
    r.markup = true;
    dispatch(std::move(r), {});
}

void XojMsgBox::showMessageToUser(GtkWindow*, const std::string& msg, GtkMessageType type) {
    xoj::compat::MessageRequest r;
    r.kind = kindOf(type);
    r.text = msg;
    dispatch(std::move(r), {});
}

void XojMsgBox::showMessageToUser(GtkWindow*, const std::string& title, const std::string& msg,
                                  GtkMessageType type) {
    xoj::compat::MessageRequest r;
    r.kind = kindOf(type);
    r.title = title;
    r.text = msg;
    dispatch(std::move(r), {});
}

void XojMsgBox::showErrorToUser(GtkWindow* win, const std::string& msg) {
    showMessageToUser(win, msg, GTK_MESSAGE_ERROR);
}

void XojMsgBox::showErrorAndQuit(std::string& msg, int exitCode) {
    g_critical("%s", msg.c_str());
    std::exit(exitCode);
}

void XojMsgBox::showPluginMessage(const std::string& pluginName, const std::string& msg, bool error) {
    xoj::compat::MessageRequest r;
    r.kind = error ? xoj::compat::MessageKind::Error : xoj::compat::MessageKind::Info;
    r.text = msg;
    r.sourcePlugin = pluginName;
    dispatch(std::move(r), {});
}

void XojMsgBox::showHelp(GtkWindow* win) { openURL(win, "https://xournalpp.github.io/"); }

void XojMsgBox::openURL(GtkWindow*, const char* url) {
    std::unique_lock lock(sinkMutex);
    if (auto& opener = urlOpenerStorage(); opener) {
        lock.unlock();
        opener(url);
        return;
    }
    lock.unlock();
    GError* error = nullptr;
    if (!g_app_info_launch_default_for_uri(url, nullptr, &error)) {
        g_warning("Could not open URL %s: %s", url, error ? error->message : "unknown error");
        g_clear_error(&error);
    }
}

void XojMsgBox::replaceFileQuestion(GtkWindow*, fs::path file,
                                    xoj::util::move_only_function<void(const fs::path&)> writeToFile) {
    if (!fs::exists(file)) {
        writeToFile(file);
        return;
    }
    xoj::compat::MessageRequest r;
    r.kind = xoj::compat::MessageKind::Question;
    r.title = _("File already exists");
    r.text = FS(_F("The file \"{1}\" already exists. Do you want to replace it?") % file.filename().u8string());
    r.buttons = {{_("Cancel"), GTK_RESPONSE_CANCEL}, {_("Replace"), GTK_RESPONSE_OK}};
    dispatch(std::move(r), [file = std::move(file), write = std::move(writeToFile)](int response) mutable {
        if (response == GTK_RESPONSE_OK) {
            write(file);
        }
    });
}
