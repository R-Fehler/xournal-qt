#include "DocumentImages.h"

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

}  // namespace xqt::DocumentImages
