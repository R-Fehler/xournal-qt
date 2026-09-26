#include "DocumentImages.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <functional>
#include <vector>

#include <glib.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "util/GzUtil.h"
#include "util/PathUtil.h"

#include "MdBox.h"

#include "MdDocument.h"
#include "TextDocument.h"

namespace xqt::DocumentImages {

namespace {
std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
}  // namespace

std::string assetsName(const fs::path& document) {
    std::string md = TextDocument::markdownName(utf8(document.filename()));  // ("name.md", archive names too)
    md.resize(md.size() - 3);
    return md + ".assets";
}

fs::path assetsFolder(const fs::path& markdownFile) {
    const std::string name = assetsName(markdownFile);
    return markdownFile.parent_path() / fs::path(std::u8string(name.begin(), name.end()));
}

md::images::Root markdownRoot(const fs::path& markdownFile) {
    return {utf8(markdownFile.parent_path()), assetsName(markdownFile), utf8(assetsFolder(markdownFile))};
}

fs::path workFolder(const fs::path& document) {
    std::error_code ec;
    const fs::path abs = fs::absolute(document, ec);
    // (FNV-1a of the path: the same folder for the same document in every run)
    uint64_t h = 1469598103934665603ULL;
    for (const unsigned char c: utf8(ec ? document : abs)) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char name[20];
    std::snprintf(name, sizeof name, "%016llx", static_cast<unsigned long long>(h));
    return Util::getCacheSubfolder("md-assets") / name;
}

void touchWorkFolder(const fs::path& document) {
    const fs::path work = workFolder(document);
    std::error_code ec;
    fs::create_directories(work, ec);
    fs::last_write_time(work, fs::file_time_type::clock::now(), ec);
}

size_t pruneWorkFolders() {
    const fs::path all = Util::getCacheSubfolder("md-assets");
    const auto limit = fs::file_time_type::clock::now() - std::chrono::hours(24 * WORK_FOLDER_DAYS);
    size_t removed = 0;
    std::error_code ec;
    for (auto it = fs::directory_iterator(all, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code fec;
        if (it->is_directory(fec) && fs::last_write_time(it->path(), fec) < limit && !fec) {
            fs::remove_all(it->path(), fec);
            removed += !fec;
        }
    }
    return removed;
}

md::images::Root embeddedRoot(const fs::path& document) {
    const fs::path work = workFolder(document);
    const std::string name = assetsName(document);
    return {utf8(work), name, utf8(work / fs::path(std::u8string(name.begin(), name.end())))};
}

md::images::Root folderRoot(const fs::path& document) { return {utf8(document.parent_path()), "", ""}; }

std::vector<std::string> carriedLinks(const std::string& markdown) {
    std::vector<std::string> out;
    const md::Document doc = md::parse(markdown);
    std::function<void(const md::Block&)> walk = [&](const md::Block& b) {
        for (const md::Run& r: b.runs) {
            if (!(r.flags & md::Image) || r.link < 0 || static_cast<size_t>(r.link) >= doc.links.size()) {
                continue;
            }
            const std::string& link = doc.links[static_cast<size_t>(r.link)];
            if (md::images::kindOf(link) != md::images::LinkKind::Local) {
                continue;
            }
            const std::string rel = md::images::percentDecoded(md::images::relativePath(link));
            if (rel.empty() || rel.find('\\') != std::string::npos || rel.back() == '/') {
                continue;
            }
            bool up = false;  // (no ".." part: the path stays below the folder it is unpacked into)
            for (size_t at = 0; at <= rel.size();) {
                const size_t slash = std::min(rel.find('/', at), rel.size());
                const std::string part = rel.substr(at, slash - at);
                up = up || part == ".." || part == "." || part.empty();
                at = slash + 1;
            }
            if (!up && std::find(out.begin(), out.end(), rel) == out.end()) {
                out.push_back(rel);
            }
        }
        for (const md::Block& c: b.children) {
            walk(c);
        }
    };
    walk(doc.root);
    return out;
}

fs::path below(const fs::path& folder, const std::string& carried) {
    if (carried.empty() || carried.front() == '/' || carried.find("..") != std::string::npos ||
        carried.find(':') != std::string::npos) {
        return {};
    }
    return folder / fs::path(std::u8string(carried.begin(), carried.end()));
}

void unpack(const fs::path& document, const fs::path& pictures) {
    std::error_code ec;
    if (pictures.empty() || !fs::is_directory(pictures, ec)) {
        return;
    }
    const fs::path work = workFolder(document);
    bool copied = false;
    for (auto it = fs::recursive_directory_iterator(pictures, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) {
            continue;
        }
        const fs::path target = work / fs::relative(it->path(), pictures, fec);
        if (fec) {
            continue;
        }
        std::error_code sec;
        if (fs::exists(target, sec) && fs::file_size(target, sec) == it->file_size(fec)) {
            continue;
        }
        fs::create_directories(target.parent_path(), sec);
        fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, sec);
        copied = copied || !sec;
    }
    if (copied) {
        md::images::changed();
    }
}

size_t copyLinked(const std::string& markdown, const fs::path& folder) {
    size_t n = 0;
    for (const std::string& carried: carriedLinks(markdown)) {
        const std::string from = md::images::resolve(carried);
        const fs::path to = below(folder, carried);
        if (from.empty() || to.empty()) {
            continue;
        }
        std::error_code ec;
        const fs::path source(std::u8string(from.begin(), from.end()));
        if (fs::exists(to, ec) && fs::equivalent(source, to, ec)) {
            ++n;
            continue;
        }
        fs::create_directories(to.parent_path(), ec);
        fs::copy_file(source, to, fs::copy_options::overwrite_existing, ec);
        n += !ec;
    }
    if (n > 0) {
        md::images::changed();
    }
    return n;
}

std::vector<std::string> carriedPicturesOf(Document& doc) {
    std::vector<std::string> out;
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        const Layer* layer = md::markdownLayer(doc.getPage(i));
        if (!layer) {
            continue;
        }
        for (const auto* e: layer->getElementsView()) {
            if (e->getType() != ELEMENT_TEXT) {
                continue;
            }
            const std::string& text = static_cast<const Text*>(e)->getText();
            if (text.find("![") == std::string::npos) {
                continue;  // (no picture: not parsed)
            }
            for (std::string& c: carriedLinks(text)) {
                if (std::find(out.begin(), out.end(), c) == out.end()) {
                    out.push_back(std::move(c));
                }
            }
        }
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> picturesData(const std::vector<std::string>& carried) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const std::string& c: carried) {
        const std::string file = md::images::resolve(c);
        if (file.empty()) {
            continue;
        }
        std::ifstream in(fs::path(std::u8string(file.begin(), file.end())), std::ios::binary);
        if (in) {
            out.emplace_back(c, std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()));
        }
    }
    return out;
}

namespace {
std::string unescaped(std::string s) {
    static const std::pair<const char*, char> entities[] = {{"&amp;", '&'}, {"&quot;", '"'}, {"&apos;", '\''},
                                                            {"&lt;", '<'},  {"&gt;", '>'}};
    std::string out;
    for (size_t i = 0; i < s.size();) {
        bool done = false;
        if (s[i] == '&') {
            for (const auto& [e, c]: entities) {
                if (s.compare(i, std::strlen(e), e) == 0) {
                    out += c;
                    i += std::strlen(e);
                    done = true;
                    break;
                }
            }
        }
        if (!done) {
            out += s[i++];
        }
    }
    return out;
}
}  // namespace

std::vector<std::pair<std::string, std::string>> xoppPictures(const fs::path& xopp) {
    std::vector<std::pair<std::string, std::string>> out;
    gzFile in = GzUtil::openPath(xopp, "r");
    if (!in) {
        return out;
    }
    std::string xml;
    char buf[1 << 16];
    for (int n; (n = gzread(in, buf, sizeof buf)) > 0;) {
        xml.append(buf, static_cast<size_t>(n));
    }
    gzclose(in);
    const std::string open = std::string("<preview ") + XOPP_PICTURE_ATTRIBUTE + "=\"";
    for (size_t at = xml.find(open); at != std::string::npos; at = xml.find(open, at + 1)) {
        const size_t nameFrom = at + open.size();
        const size_t nameTo = xml.find('"', nameFrom);
        const size_t dataFrom = nameTo == std::string::npos ? nameTo : xml.find('>', nameTo);
        const size_t dataTo = dataFrom == std::string::npos ? dataFrom : xml.find("</preview>", dataFrom);
        if (dataTo == std::string::npos) {
            break;
        }
        std::string base64 = xml.substr(dataFrom + 1, dataTo - dataFrom - 1);
        gsize length = 0;
        guchar* data = g_base64_decode(base64.c_str(), &length);
        out.emplace_back(unescaped(xml.substr(nameFrom, nameTo - nameFrom)),
                         std::string(reinterpret_cast<const char*>(data), length));
        g_free(data);
        at = dataTo;
    }
    return out;
}

bool unpackXopp(const fs::path& xopp) {
    const auto pictures = xoppPictures(xopp);
    if (pictures.empty()) {
        return false;
    }
    const fs::path work = workFolder(xopp);
    for (const auto& [name, data]: pictures) {
        const fs::path target = below(work, name);
        if (target.empty()) {
            continue;
        }
        std::error_code ec;
        if (fs::exists(target, ec) && fs::file_size(target, ec) == data.size()) {
            continue;
        }
        fs::create_directories(target.parent_path(), ec);
        std::ofstream(target, std::ios::binary) << data;
    }
    md::images::changed();
    return true;
}

std::string exportPictures(const std::string& markdown, const fs::path& mdFile, size_t& copied) {
    copied = 0;
    const std::string target = assetsName(mdFile);
    const fs::path folder = mdFile.parent_path();
    std::string text = markdown;
    std::vector<std::string> renamedFolders;
    for (const std::string& carried: carriedLinks(markdown)) {
        const std::string from = md::images::resolve(carried);
        std::string to = carried;
        const size_t slash = carried.find('/');
        const std::string first = slash == std::string::npos ? std::string() : carried.substr(0, slash);
        if (first.size() > 7 && first.compare(first.size() - 7, 7, ".assets") == 0 && first != target) {
            to = target + carried.substr(slash);
            if (std::find(renamedFolders.begin(), renamedFolders.end(), first) == renamedFolders.end()) {
                renamedFolders.push_back(first);
                text = renamedAssetLinks(text, first, target);
            }
        }
        const fs::path dest = below(folder, to);
        if (from.empty() || dest.empty()) {
            continue;
        }
        std::error_code ec;
        const fs::path source(std::u8string(from.begin(), from.end()));
        if (fs::exists(dest, ec) && fs::equivalent(source, dest, ec)) {
            continue;
        }
        fs::create_directories(dest.parent_path(), ec);
        fs::copy_file(source, dest, fs::copy_options::overwrite_existing, ec);
        copied += !ec;
    }
    return text;
}

std::vector<fs::path> unusedPictures(const fs::path& markdownFile, const std::string& text) {
    std::vector<fs::path> out;
    const fs::path folder = assetsFolder(markdownFile);
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        return out;
    }
    // What the text links to, as paths relative to the .md's folder (its links and pictures; HTML's src="…")
    std::vector<std::string> linked;
    const md::Document doc = md::parse(text);
    std::vector<std::string> links = doc.links;
    for (size_t at = text.find("src="); at != std::string::npos; at = text.find("src=", at + 4)) {
        const char quote = at + 4 < text.size() ? text[at + 4] : 0;
        if (quote == '"' || quote == '\'') {
            const size_t end = text.find(quote, at + 5);
            if (end != std::string::npos) {
                links.push_back(text.substr(at + 5, end - at - 5));
            }
        }
    }
    for (const std::string& link: links) {
        const std::string rel = md::images::relativePath(link);
        if (!rel.empty()) {
            linked.push_back(rel);
            linked.push_back(md::images::percentDecoded(rel));
        }
    }
    const std::string prefix = assetsName(markdownFile) + "/";
    for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) {
            continue;
        }
        const std::u8string inFolder = fs::relative(it->path(), folder, fec).generic_u8string();
        const std::string rel = prefix + std::string(inFolder.begin(), inFolder.end());
        if (std::find(linked.begin(), linked.end(), rel) == linked.end()) {
            out.push_back(it->path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string linkEncoded(const std::string& name) {
    std::string out;
    for (const char c: name) {
        switch (c) {
            case ' ':
                out += "%20";
                break;
            case '(':
                out += "%28";
                break;
            case ')':
                out += "%29";
                break;
            case '<':
                out += "%3C";
                break;
            case '>':
                out += "%3E";
                break;
            case '%':
                out += "%25";
                break;
            case '#':
                out += "%23";
                break;
            case '?':
                out += "%3F";
                break;
            default:
                out += c;
        }
    }
    return out;
}

std::string renamedAssetLinks(const std::string& text, const std::string& oldName, const std::string& newName) {
    if (oldName.empty() || oldName == newName) {
        return text;
    }
    std::vector<std::string> spellings{oldName};
    if (linkEncoded(oldName) != oldName) {
        spellings.push_back(linkEncoded(oldName));
    }
    std::string out = text;
    for (const std::string& from: spellings) {
        const std::string needle = from + "/";
        for (size_t at = out.find(needle); at != std::string::npos; at = out.find(needle, at + 1)) {
            // What is before it: "./", "<", then where a link's address starts
            size_t q = at;
            if (q >= 2 && out.compare(q - 2, 2, "./") == 0) {
                q -= 2;
            }
            const bool angle = q >= 1 && out[q - 1] == '<';
            if (angle) {
                --q;
            }
            bool link = q >= 1 && out[q - 1] == '(';
            if (!link) {  // a reference definition "[x]: old/…" (blanks after the colon)
                size_t k = q;
                while (k > 0 && (out[k - 1] == ' ' || out[k - 1] == '\t')) {
                    --k;
                }
                link = k >= 2 && out[k - 1] == ':' && out[k - 2] == ']';
            }
            if (!link) {  // HTML: src="old/…"
                link = q >= 5 && (out.compare(q - 5, 5, "src=\"") == 0 || out.compare(q - 5, 5, "src='") == 0);
            }
            if (link) {
                // (between <>, a name may have blanks; elsewhere it is written as an address)
                const std::string to = angle ? newName : linkEncoded(newName);
                out.replace(at, from.size(), to);
                at += to.size() - 1;
            }
        }
    }
    return out;
}

}  // namespace xqt::DocumentImages
