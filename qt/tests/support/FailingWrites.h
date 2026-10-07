/*
 * xournal-qt: test helpers that make writing fail, also for root. CI builds and tests in containers as root, and root
 * writes into folders without write permission, so a folder made read-only is not a failure there.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <csignal>
#include <string>

#ifndef _WIN32
#include <sys/resource.h>
#include <unistd.h>
#endif

#include "filesystem.h"

namespace xqt::test {

/// Whether a folder without write permission is closed to this process (not for root, or with CAP_DAC_OVERRIDE).
inline bool permissionsBind() {
#ifdef _WIN32
    return false;  // (a read-only folder can be written on Windows)
#else
    static const bool binds = [] {
        const fs::path dir = fs::temp_directory_path() / ("xqt-permissions-" + std::to_string(getpid()));
        std::error_code ec;
        fs::create_directories(dir, ec);
        fs::permissions(dir, fs::perms::owner_read | fs::perms::owner_exec, ec);
        const bool closed = access(dir.c_str(), W_OK) != 0;
        fs::permissions(dir, fs::perms::owner_all, ec);
        fs::remove(dir, ec);
        return closed;
    }();
    return binds;
#endif
}

#ifndef _WIN32
/// While it lives, no file of this process grows beyond `bytes`: writing more fails (EFBIG) as on a full disk, for
/// root too. For all threads of the process: only while nothing else writes.
class FileSizeLimit {
public:
    explicit FileSizeLimit(rlim_t bytes) {
        getrlimit(RLIMIT_FSIZE, &before);
        rlimit limit = before;
        limit.rlim_cur = bytes;
        setrlimit(RLIMIT_FSIZE, &limit);
        handler = std::signal(SIGXFSZ, SIG_IGN);  // (else the process is ended)
    }
    ~FileSizeLimit() {
        setrlimit(RLIMIT_FSIZE, &before);
        std::signal(SIGXFSZ, handler);
    }
    FileSizeLimit(const FileSizeLimit&) = delete;
    FileSizeLimit& operator=(const FileSizeLimit&) = delete;

private:
    rlimit before{};
    void (*handler)(int) = SIG_DFL;
};
#endif

}  // namespace xqt::test
