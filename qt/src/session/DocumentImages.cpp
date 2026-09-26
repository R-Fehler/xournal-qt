#include "DocumentImages.h"

#include <vector>

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
