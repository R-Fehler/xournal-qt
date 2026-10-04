#include "AudioFiles.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>

#include <QStandardPaths>

namespace xqt::audio {

namespace {
std::mutex m;
fs::path appOverride;
std::vector<fs::path> extra;
std::vector<fs::path> opened;  ///< (a folder may be added more than once: each handle takes one away)
std::map<fs::path, fs::path> extracted;  ///< document -> its recordings in the cache

fs::path utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

bool isFile(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(p, ec);
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
        folders.push_back(documentFile.parent_path());
        folders.push_back(exportFolderOf(documentFile));
    }
    folders.push_back(appFolder());
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

size_t adoptExtracted(const std::vector<std::string>& names, const fs::path& documentFile) {
    std::vector<fs::path> from;
    {
        std::lock_guard lock(m);
        for (const auto& [doc, folder]: extracted) {
            from.push_back(folder);
        }
    }
    const fs::path app = appFolder();
    size_t copied = 0;
    for (const auto& n: names) {
        const fs::path p = utf8Path(n);
        const fs::path found = find(n, documentFile);
        if (found.empty() || p.is_absolute()) {
            continue;
        }
        if (std::find(from.begin(), from.end(), found.parent_path()) == from.end()) {
            continue;  // (next to the document, in the app's folder or another folder of the user: found as it is)
        }
        std::error_code ec;
        if (fs::copy_file(found, app / p.filename(), fs::copy_options::skip_existing, ec) && !ec) {
            ++copied;
        }
    }
    return copied;
}

fs::path exportFolderOf(const fs::path& xopp) {
    fs::path f = xopp;
    f.replace_extension(".audio");
    return f;
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
