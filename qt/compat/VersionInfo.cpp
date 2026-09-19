/*
 * xournal-qt: GTK-free implementation of upstream util/VersionInfo.h.
 *
 * @license GNU GPLv2 or later
 */
#include "util/VersionInfo.h"

#include <sstream>

#include <cairo.h>
#include <glib.h>
#include <poppler.h>

#include "util/raii/CStringWrapper.h"

#include "config-git.h"
#include "config.h"

namespace xoj::util {
const char* getGdkBackend() { return nullptr; }

std::string getXournalppVersion() {
    auto str = std::string(PROJECT_NAME) + " " + PROJECT_VERSION;
    if (!std::string(GIT_COMMIT_ID).empty()) {
        str = str + " (" + GIT_COMMIT_ID + " from " + GIT_BRANCH + ")";
    }
    return str;
}

std::string getOsInfo() {
    auto osInfo = xoj::util::OwnedCString::assumeOwnership(g_get_os_info(G_OS_INFO_KEY_NAME));
    if (!osInfo) {
        osInfo = xoj::util::OwnedCString::assumeOwnership(g_get_os_info(G_OS_INFO_KEY_PRETTY_NAME));
    }
    if (osInfo) {
        xoj::util::OwnedCString osVersion;
        for (auto key: {G_OS_INFO_KEY_VERSION, G_OS_INFO_KEY_VERSION_ID, G_OS_INFO_KEY_VERSION_CODENAME}) {
            osVersion = xoj::util::OwnedCString::assumeOwnership(g_get_os_info(key));
            if (osVersion) {
                break;
            }
        }
        return osVersion ? std::string(osInfo.get()) + " " + osVersion.get() : std::string(osInfo.get());
    }
    return std::string();
}

std::string getVersionInfo() {
    std::stringstream str;
    str.imbue(std::locale::classic());
    str << getXournalppVersion() << std::endl;
    str << "├──glib: " << glib_major_version << "." << glib_minor_version << "." << glib_micro_version << std::endl;
    str << "├──cairo:  " << cairo_version_string() << std::endl;
    str << "├──poppler:  " << poppler_get_version() << std::endl;
    str << "└──OS info: " << getOsInfo() << std::endl;
    return str.str();
}
}  // namespace xoj::util
