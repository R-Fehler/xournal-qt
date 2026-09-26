/*
 * xournal-qt: emoji by name (qt/src/markdown/EmojiData.h): shortcodes, their completion, and ":smile:" shown as 😄
 * in Markdown.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <pango/pango.h>

#include "EmojiData.h"
#include "MdLayout.h"

using namespace xqt;

namespace {

const std::string SMILE = "\xf0\x9f\x98\x84";          // 😄
const std::string SMILEY = "\xf0\x9f\x98\x83";         // 😃
const std::string THUMBS_UP = "\xf0\x9f\x91\x8d";      // 👍
const std::string HEART_EYES = "\xf0\x9f\x98\x8d";     // 😍

std::vector<std::string> names(const std::vector<emoji::Completion>& cs) {
    std::vector<std::string> out;
    for (const auto& c: cs) {
        out.emplace_back(c.name);
    }
    return out;
}

md::Layout lay(const std::string& src, size_t active = md::NO_SOURCE) {
    md::Style s;
    s.width = 400;
    return md::layout(md::parse(src), s, src, active);
}

/// The Pango text of every text item, joined by "|".
std::string shown(const md::Layout& l) {
    std::string out;
    for (const md::Item& it: l.items) {
        if (it.kind == md::Item::Kind::Text) {
            out += (out.empty() ? "" : "|") + std::string(pango_layout_get_text(it.layout.get()));
        }
    }
    return out;
}

}  // namespace

TEST(EmojiData, shortcodesAreGitHubs) {
    EXPECT_EQ(emoji::byShortcode("smile"), SMILE);
    EXPECT_EQ(emoji::byShortcode("+1"), THUMBS_UP);
    EXPECT_EQ(emoji::byShortcode("thumbsup"), THUMBS_UP);
    EXPECT_EQ(emoji::byShortcode("heart_eyes"), HEART_EYES);
    EXPECT_EQ(emoji::byShortcode("woman_technologist"), "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb");
    EXPECT_EQ(emoji::byShortcode("de"), "\xf0\x9f\x87\xa9\xf0\x9f\x87\xaa");  // (flags by country code)
    EXPECT_TRUE(emoji::byShortcode("smil").empty());
    EXPECT_TRUE(emoji::byShortcode("no_such_emoji").empty());
    EXPECT_EQ(emoji::all().size(), 1870u);
    EXPECT_EQ(emoji::categories().size(), 9u);
}

TEST(EmojiData, theShortcodeBeingTyped) {
    EXPECT_EQ(emoji::typedShortcode("Hi :smi"), "smi");
    EXPECT_EQ(emoji::typedShortcode(":sm"), "sm");
    EXPECT_EQ(emoji::typedShortcode("(:+1"), "+1");
    EXPECT_EQ(emoji::typedShortcode("a\n:heart_ey"), "heart_ey");
    EXPECT_EQ(emoji::typedShortcode("Hi :s"), "") << "at least two letters";
    EXPECT_EQ(emoji::typedShortcode("Hi :"), "");
    EXPECT_EQ(emoji::typedShortcode("at 10:30"), "") << "not in a time";
    EXPECT_EQ(emoji::typedShortcode("std::vec"), "") << "not after another colon";
    EXPECT_EQ(emoji::typedShortcode("word:ab"), "") << "the colon begins a word";
    EXPECT_EQ(emoji::typedShortcode(":smile: "), "") << "a space ends it";
    EXPECT_EQ(emoji::typedShortcode(":smile:"), "") << "a colon ends it";
}

TEST(EmojiData, completionShowsOneWordShortcodesFirst) {
    const auto smi = emoji::complete("smi", 6);
    ASSERT_GE(smi.size(), 3u);
    EXPECT_EQ(smi[0].name, "smile");
    EXPECT_EQ(std::string(smi[0].emoji->emoji), SMILE);
    EXPECT_EQ(smi[1].name, "smiley");
    EXPECT_EQ(std::string(smi[1].emoji->emoji), SMILEY);
    EXPECT_EQ(smi[2].name, "smirk");
    EXPECT_EQ(smi[3].name, "smile_cat");
    EXPECT_EQ(smi.size(), 6u) << "at most the limit";
    for (const auto& c: smi) {
        EXPECT_EQ(c.name.substr(0, 3), "smi") << c.name;
    }
    // Then a later word of a shortcode, then a tag
    const auto all = names(emoji::complete("smi", 100));
    const auto at = [&](const std::string& n) { return std::find(all.begin(), all.end(), n) - all.begin(); };
    EXPECT_LT(at("smiling_imp"), at("slightly_smiling_face"));
    EXPECT_LT(at("slightly_smiling_face"), static_cast<long>(all.size()));
    // Upper case is taken as lower case; each emoji once
    EXPECT_EQ(names(emoji::complete("SMI", 6)), names(smi));
    const auto thumbs = emoji::complete("thumbs", 10);
    ASSERT_GE(thumbs.size(), 2u);
    EXPECT_EQ(thumbs[0].name, "thumbsdown");
    EXPECT_EQ(thumbs[1].name, "thumbsup");
    EXPECT_EQ(std::count_if(thumbs.begin(), thumbs.end(), [](const auto& c) { return c.emoji->emoji == THUMBS_UP; }), 1);
    EXPECT_TRUE(emoji::complete("zzzq").empty());
}

TEST(EmojiData, pickerSearch) {
    EXPECT_EQ(emoji::search("").size(), 1870u);
    const auto heart = emoji::search("heart");
    ASSERT_FALSE(heart.empty());
    EXPECT_EQ(std::string(heart[0]->aliases).substr(0, 5), "heart") << "a shortcode that begins with it first";
    const auto happy = emoji::search("happy");  // (a tag)
    EXPECT_NE(std::find_if(happy.begin(), happy.end(), [](auto* e) { return e->emoji == SMILE; }), happy.end());
}

TEST(EmojiData, shortcodesInAText) {
    const auto found = emoji::findShortcodes("a :smile: b :nope: :+1::heart_eyes: c:d");
    ASSERT_EQ(found.size(), 3u);
    EXPECT_EQ(found[0].at, 2u);
    EXPECT_EQ(found[0].length, 7u);
    EXPECT_EQ(found[0].emoji, SMILE);
    EXPECT_EQ(found[1].emoji, THUMBS_UP);
    EXPECT_EQ(found[2].emoji, HEART_EYES);
    EXPECT_EQ(emoji::findShortcodes(":not:smile:").size(), 1u) << "a closing colon can open the next one";
    EXPECT_TRUE(emoji::findShortcodes("10:30:45 and :SMILE:").empty());
}

/// Markdown shows ":smile:" as 😄 and keeps it in the source; not in code, and not in the block being written.
TEST(EmojiData, markdownShowsShortcodesAsEmoji) {
    const std::string src = "Hi :smile: and **:+1:** and `:smile:` :nope:\n\n```\n:smile:\n```\n\n# A :heart_eyes:\n";
    const md::Layout l = lay(src);
    EXPECT_EQ(shown(l), "Hi " + SMILE + " and " + THUMBS_UP + " and :smile: :nope:|:smile:|A " + HEART_EYES);

    // The emoji stands for its shortcode in the source: a place in it is the whole shortcode
    const md::Item& first = l.items[static_cast<size_t>(l.blocks[0].item)];
    bool mapped = false;
    for (const md::SourceMap& m: first.sources) {
        if (std::string(pango_layout_get_text(first.layout.get()) + m.start, static_cast<size_t>(m.length)) == SMILE) {
            EXPECT_EQ(m.source, 3u);
            EXPECT_EQ(m.sourceLength, 7u);
            mapped = true;
        }
    }
    EXPECT_TRUE(mapped);
    const auto rects = md::sourceRects(l, 5, 6);  // (inside ":smile:")
    ASSERT_EQ(rects.size(), 1u);
    EXPECT_GT(rects[0].width, 5.0) << "the whole emoji";

    // Being written: the paragraph with the cursor shows its source, the others their emoji
    const md::Layout editing = lay(src, 1);
    EXPECT_NE(shown(editing).find("Hi :smile: and **:+1:**"), std::string::npos);
    EXPECT_NE(shown(editing).find("A " + HEART_EYES), std::string::npos);

    // A plain text (a .txt) shows it as it is
    const std::string txt = std::string(md::PLAIN_MARKER) + "\nHi :smile:\n";
    EXPECT_EQ(shown(lay(txt)), "Hi :smile:|");
}
