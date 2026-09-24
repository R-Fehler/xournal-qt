#include "DocumentMode.h"

#include <string>

#include <QtGlobal>

#include "control/settings/Settings.h"

namespace xqt::DocumentMode {

namespace {
constexpr const char* KEY = "documentMode";

Mode fromEnvironment() { return fromName(qEnvironmentVariable("XQT_DOCUMENT_MODE").trimmed().toLower()); }
}  // namespace

QString nameOf(Mode mode) {
    switch (mode) {
        case Mode::Xopp:
            return QStringLiteral("xopp");
        case Mode::Pdf:
            return QStringLiteral("pdf");
        case Mode::Unset:
            break;
    }
    return {};
}

Mode fromName(const QString& name) {
    if (name == QLatin1String("xopp")) {
        return Mode::Xopp;
    }
    if (name == QLatin1String("pdf")) {
        return Mode::Pdf;
    }
    return Mode::Unset;
}

Mode stored(Settings& settings) {
    std::string v;
    settings.getCustomElement("xournalQt").getString(KEY, v);
    return fromName(QString::fromStdString(v));
}

Mode effective(Settings& settings) {
    if (const Mode m = stored(settings); m != Mode::Unset) {
        return m;
    }
    if (const Mode m = fromEnvironment(); m != Mode::Unset) {
        return m;
    }
    return Mode::Xopp;  // (as before the question existed)
}

bool pdfOnly(Settings& settings) { return effective(settings) == Mode::Pdf; }

bool shouldAsk(Settings& settings) { return stored(settings) == Mode::Unset && fromEnvironment() == Mode::Unset; }

void store(Settings& settings, Mode mode) {
    settings.getCustomElement("xournalQt").setString(KEY, nameOf(mode).toStdString());  // ("": not chosen)
    settings.customSettingsChanged();
}

}  // namespace xqt::DocumentMode
