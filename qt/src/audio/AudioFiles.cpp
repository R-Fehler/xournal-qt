#include "AudioFiles.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>

#include <QStandardPaths>

namespace xqt::audio {

namespace {
std::mutex m;
fs::path appOverride;
std::vector<fs::path> extra;
std::vector<fs::path> opened;  ///< (a folder may be added more than once: each handle takes one away)
std::map<fs::path, fs::path> extracted;  ///< document -> its recordings in the cache
std::set<fs::path> busy;                  ///< recordings being written

fs::path utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

bool isFile(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(p, ec);
}

std::string lowerExtension(const fs::path& p) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

bool isBusy(const fs::path& file) {
    std::lock_guard lock(m);
    return busy.count(file.lexically_normal()) > 0;
}

std::string pageTag(size_t page) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "p%03zu", page + 1);
    return buf;
}
}  // namespace

fs::path appFolder() {
    fs::path folder;
    {
        std::lock_guard lock(m);
        folder = appOverride;
    }
    if (folder.empty()) {
        folder = fs::path(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdU16String()) / "audio";
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    return folder;
}

void setAppFolder(const fs::path& folder) {
    std::lock_guard lock(m);
    appOverride = folder;
}

void setExtraFolders(std::vector<fs::path> folders) {
    std::lock_guard lock(m);
    extra = std::move(folders);
}

FolderHandle::FolderHandle(fs::path f): folder(std::move(f)) {
    std::lock_guard lock(m);
    opened.push_back(folder);
}

FolderHandle::~FolderHandle() {
    std::lock_guard lock(m);
    if (auto it = std::find(opened.begin(), opened.end(), folder); it != opened.end()) {
        opened.erase(it);
    }
}

std::unique_ptr<FolderHandle> addFolder(const fs::path& folder) { return std::make_unique<FolderHandle>(folder); }

fs::path find(const std::string& name, const fs::path& documentFile) {
    if (name.empty()) {
        return {};
    }
    const fs::path p = utf8Path(name);
    if (p.is_absolute() && isFile(p)) {
        return p;
    }
    const fs::path bare = p.filename();
    std::vector<fs::path> folders;
    if (!documentFile.empty()) {
        folders.push_back(sidecarOf(documentFile));
        folders.push_back(documentFile.parent_path());
    }
    if (!keepsSidecar(documentFile)) {
        folders.push_back(appFolder());  // (not saved yet, a PDF with notes: they record there)
    }
    {
        std::lock_guard lock(m);
        if (auto it = extracted.find(documentFile.lexically_normal()); !documentFile.empty() && it != extracted.end()) {
            folders.push_back(it->second);
        }
        folders.insert(folders.end(), extra.begin(), extra.end());
        folders.insert(folders.end(), opened.rbegin(), opened.rend());
        for (const auto& [doc, folder]: extracted) {
            folders.push_back(folder);
        }
    }
    for (const auto& f: folders) {
        if (!f.empty() && isFile(f / bare)) {
            return f / bare;
        }
    }
    return {};
}

void setExtractedFolder(const fs::path& document, const fs::path& folder) {
    std::lock_guard lock(m);
    extracted[document.lexically_normal()] = folder;
}

bool keepsSidecar(const fs::path& documentFile) {
    const std::string e = lowerExtension(documentFile);
    return e == ".xopp" || e == ".xoj";
}

fs::path sidecarOf(const fs::path& documentFile) {
    fs::path f = documentFile;
    f.replace_extension(".audio");
    return f;
}

fs::path recordingFolderFor(const fs::path& documentFile) {
    if (!keepsSidecar(documentFile)) {
        return appFolder();
    }
    const fs::path folder = sidecarOf(documentFile);
    std::error_code ec;
    fs::create_directories(folder, ec);
    return folder;
}

void setBusy(const fs::path& file, bool on) {
    std::lock_guard lock(m);
    if (on) {
        busy.insert(file.lexically_normal());
    } else {
        busy.erase(file.lexically_normal());
    }
}

namespace {
/// Copies `file` to `target` (not over a file there); checked by its size. A half copy is removed.
bool copyChecked(const fs::path& file, const fs::path& target) {
    std::error_code ec;
    if (!fs::copy_file(file, target, fs::copy_options::none, ec) || ec) {
        return false;
    }
    std::error_code a, b;
    if (fs::file_size(file, a) != fs::file_size(target, b) || a || b) {
        fs::remove(target, ec);
        return false;
    }
    return true;
}
}  // namespace

bool moveInto(const fs::path& file, const fs::path& into) {
    std::error_code ec;
    fs::create_directories(into, ec);
    const fs::path target = into / file.filename();
    if (fs::exists(target, ec)) {
        return fs::equivalent(file, target, ec);
    }
    fs::rename(file, target, ec);
    if (!ec) {
        return true;
    }
    if (!copyChecked(file, target)) {  // (another disk)
        return false;
    }
    fs::remove(file, ec);
    return true;
}

size_t gather(const std::vector<std::string>& names, const fs::path& from, const fs::path& to) {
    if (!keepsSidecar(to)) {
        return 0;
    }
    const fs::path into = sidecarOf(to);
    const fs::path app = appFolder();
    size_t done = 0;
    for (const auto& n: names) {
        const fs::path p = utf8Path(n);
        if (n.empty() || p.is_absolute()) {
            continue;  // (Export for Xournal++'s names: found as they are)
        }
        std::error_code ec;
        const fs::path target = into / p.filename();
        if (isFile(target)) {
            continue;
        }
        const fs::path found = find(n, from);
        if (found.empty() || isBusy(found) || fs::equivalent(found.parent_path(), into, ec)) {
            continue;  // (being recorded: AudioControl takes it when it ends)
        }
        const bool own = from.empty() && fs::equivalent(found.parent_path(), app, ec);
        fs::create_directories(into, ec);
        if (own ? moveInto(found, into) : copyChecked(found, target)) {
            ++done;
        }
    }
    return done;
}

std::string attachmentName(const std::string& name, const std::vector<size_t>& pages) {
    const std::u8string bare = utf8Path(name).filename().u8string();
    std::string out = "audio-";
    if (!pages.empty()) {
        out += pageTag(pages.front()) + "-";
        if (pages.back() != pages.front()) {
            out += pageTag(pages.back()) + "-";
        }
    }
    return out + std::string(bare.begin(), bare.end());
}

std::string attachmentDescription(const std::string& name, const std::vector<size_t>& pages) {
    std::string out = "Audio recording " + std::string(reinterpret_cast<const char*>(utf8Path(name).stem().u8string().c_str()));
    if (!pages.empty()) {
        out += pages.size() == 1 && pages.front() == pages.back()
                       ? ", page " + std::to_string(pages.front() + 1)
                       : ", pages " + std::to_string(pages.front() + 1) + " to " + std::to_string(pages.back() + 1);
    }
    return out + " (Ogg Vorbis; xournal-qt: ink written while it ran plays it in the app)";
}

}  // namespace xqt::audio
