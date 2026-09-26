/*
 * xournal-qt: upstream's SaveHandler, with the pictures of the document's Markdown texts at the end of the document
 * as extra <preview> elements (DocumentImages.h, "pictures inside a .xopp"; qt/docs/md-images.md). A document without
 * pictures is written exactly as upstream writes it.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "control/xml/XmlTexNode.h"
#include "control/xojfile/SaveHandler.h"

#include "DocumentImages.h"

namespace xqt {

class PictureSaveHandler final: public SaveHandler {
public:
    /// After prepareSave: the pictures (their carried paths and data: DocumentImages::picturesData).
    void addPictures(std::vector<std::pair<std::string, std::string>> pictures) {
        for (auto& [name, data]: pictures) {
            constexpr std::u8string_view tag = u8"preview";
            auto* node = new XmlTexNode(tag, std::move(data));  // (base64, as a TeX image's data)
            node->setAttrib(reinterpret_cast<const char8_t*>(DocumentImages::XOPP_PICTURE_ATTRIBUTE), name);
            root->addChild(node);  // (the document owns it)
        }
    }
};

}  // namespace xqt
