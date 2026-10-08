#include "InkTextLayer.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>

#include "FileIo.h"

namespace xqt::InkTextLayer {

std::vector<Word> wordsOf(const ink::PageText& page) {
    std::vector<Word> out;
    for (const ink::Word& w: page.words) {
        if (w.candidates.empty() || w.candidates.front().p < MIN_P || w.conf < MIN_CONF || w.box.isEmpty()) {
            continue;
        }
        QString text;
        for (const QChar c: w.text) {
            if (!c.isSurrogate() && c.unicode() >= 0x20) {
                text += c;
            }
        }
        if (!text.trimmed().isEmpty()) {
            out.push_back({text.trimmed(), w.box, w.angle});
        }
    }
    return out;
}

namespace {
std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    std::string s(buf);
    while (s.size() > 1 && s.back() == '0') {
        s.pop_back();
    }
    if (!s.empty() && s.back() == '.') {
        s.pop_back();
    }
    return s == "-0" ? "0" : s;
}

/// With 4 decimals (the text matrix's turn)
std::string num4(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    std::string s(buf);
    while (s.size() > 1 && s.back() == '0') {
        s.pop_back();
    }
    if (!s.empty() && s.back() == '.') {
        s.pop_back();
    }
    return s == "-0" ? "0" : s;
}

std::string hexOf(const QString& text) {
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    for (const QChar c: text) {
        const auto u = c.unicode();
        for (int shift = 12; shift >= 0; shift -= 4) {
            out += digits[(u >> shift) & 0xF];
        }
    }
    return out;
}
}  // namespace

std::string contentOf(const std::vector<Word>& words, double pageHeight, const std::string& cm) {
    if (words.empty()) {
        return {};
    }
    std::string s = "q\n";
    if (!cm.empty()) {
        s += cm + " cm\n";
    }
    s += "BT\n3 Tr\n";
    for (const Word& w: words) {
        const double size = std::max(1.0, w.box.height());
        const QString text = w.text + u' ';
        // Stretched so the word (without its space) covers its box
        const double natural = static_cast<double>(w.text.size()) * ADVANCE / 1000.0 * size;
        const double tz = natural > 0 ? 100.0 * w.box.width() / natural : 100.0;
        // (the baseline a fifth of the size above the box's bottom: the font's descent)
        std::string tm;
        if (w.angle == 0) {
            tm = "1 0 0 1 " + num(w.box.left()) + ' ' + num(pageHeight - w.box.bottom() + size * -DESCENT / 1000.0);
        } else {
            // Turned with the word: its baseline's start, turned around its middle; the text's x axis along the
            // direction it was written in (y up in PDF: clockwise on the page is counter-clockwise there)
            const QPointF baseline(-w.box.width() / 2, w.box.height() / 2 + size * DESCENT / 1000.0);
            const QPointF start = w.box.center() + ink::turned(baseline, w.angle);
            const QPointF x = ink::turned(QPointF(1, 0), w.angle);
            tm = num4(x.x()) + ' ' + num4(-x.y()) + ' ' + num4(x.y()) + ' ' + num4(x.x()) + ' ' + num(start.x()) + ' ' +
                 num(pageHeight - start.y());
        }
        s += std::string(FONT_RESOURCE) + ' ' + num(size) + " Tf " + num(tz) + " Tz " + tm + " Tm <" + hexOf(text) +
             "> Tj\n";
    }
    s += "ET\nQ\n";
    return s;
}

std::string sigOf(const std::string& content) {
    return fileio::hex16(fileio::fnv1a(content));
}

// --- the font -------------------------------------------------------------------------------------------------------

namespace {
struct Bytes {
    std::string data;
    void u8(unsigned v) { data += static_cast<char>(v & 0xFF); }
    void u16(unsigned v) {
        u8(v >> 8);
        u8(v);
    }
    void u32(uint32_t v) {
        u16(v >> 16);
        u16(v & 0xFFFF);
    }
    void zeros(size_t n) { data.append(n, '\0'); }
};

uint32_t checksum(const std::string& table) {
    std::string t = table;
    t.append((4 - t.size() % 4) % 4, '\0');
    uint32_t sum = 0;
    for (size_t i = 0; i < t.size(); i += 4) {
        sum += (static_cast<uint32_t>(static_cast<unsigned char>(t[i])) << 24) |
               (static_cast<uint32_t>(static_cast<unsigned char>(t[i + 1])) << 16) |
               (static_cast<uint32_t>(static_cast<unsigned char>(t[i + 2])) << 8) |
               static_cast<uint32_t>(static_cast<unsigned char>(t[i + 3]));
    }
    return sum;
}

std::string makeFont() {
    std::map<std::string, std::string> tables;  // (sorted by tag, as the directory wants them)
    {
        Bytes cmap;  // format 4, one segment (0xFFFF): no character maps to a glyph (a CID font needs none)
        cmap.u16(0);
        cmap.u16(1);
        cmap.u16(3);
        cmap.u16(1);
        cmap.u32(12);
        cmap.u16(4);
        cmap.u16(24);
        cmap.u16(0);
        cmap.u16(2);  // segCountX2
        cmap.u16(2);  // searchRange
        cmap.u16(0);  // entrySelector
        cmap.u16(0);  // rangeShift
        cmap.u16(0xFFFF);
        cmap.u16(0);
        cmap.u16(0xFFFF);
        cmap.u16(1);
        cmap.u16(0);
        tables["cmap"] = cmap.data;
    }
    tables["glyf"] = std::string();  // (both glyphs empty)
    {
        Bytes head;
        head.u32(0x00010000);
        head.u32(0x00010000);
        head.u32(0);  // checkSumAdjustment (below)
        head.u32(0x5F0F3CF5);
        head.u16(0x000B);
        head.u16(1000);  // unitsPerEm
        head.zeros(16);  // created, modified
        head.u16(0);
        head.u16(static_cast<unsigned>(DESCENT) & 0xFFFF);
        head.u16(ADVANCE);
        head.u16(ASCENT);
        head.u16(0);  // macStyle
        head.u16(3);  // lowestRecPPEM
        head.u16(2);  // fontDirectionHint
        head.u16(0);  // indexToLocFormat: short
        head.u16(0);
        tables["head"] = head.data;
    }
    {
        Bytes hhea;
        hhea.u32(0x00010000);
        hhea.u16(ASCENT);
        hhea.u16(static_cast<unsigned>(DESCENT) & 0xFFFF);
        hhea.u16(0);     // lineGap
        hhea.u16(ADVANCE);
        hhea.u16(0);
        hhea.u16(0);
        hhea.u16(0);
        hhea.u16(1);  // caretSlopeRise
        hhea.u16(0);
        hhea.u16(0);
        hhea.zeros(8);
        hhea.u16(0);
        hhea.u16(2);  // numberOfHMetrics
        tables["hhea"] = hhea.data;
    }
    {
        Bytes hmtx;
        for (int g = 0; g < 2; ++g) {
            hmtx.u16(ADVANCE);
            hmtx.u16(0);
        }
        tables["hmtx"] = hmtx.data;
    }
    {
        Bytes loca;
        loca.zeros(6);
        tables["loca"] = loca.data;
    }
    {
        Bytes maxp;
        maxp.u32(0x00010000);
        maxp.u16(2);  // numGlyphs
        maxp.zeros(8);
        maxp.u16(2);  // maxZones
        maxp.zeros(16);
        tables["maxp"] = maxp.data;
    }
    {
        Bytes post;
        post.u32(0x00030000);
        post.u32(0);
        post.u16(static_cast<unsigned>(-100) & 0xFFFF);
        post.u16(50);
        post.u32(1);
        post.zeros(16);
        tables["post"] = post.data;
    }
    const auto n = static_cast<unsigned>(tables.size());
    Bytes font;
    font.u32(0x00010000);
    font.u16(n);
    unsigned power = 1, log = 0;
    while (power * 2 <= n) {
        power *= 2;
        ++log;
    }
    font.u16(power * 16);
    font.u16(log);
    font.u16(n * 16 - power * 16);
    uint32_t offset = 12 + 16 * n;
    std::string body;
    size_t headAt = 0;
    for (const auto& [tag, data]: tables) {
        font.data += tag;
        font.u32(checksum(data));
        font.u32(offset);
        font.u32(static_cast<uint32_t>(data.size()));
        if (tag == "head") {
            headAt = offset;
        }
        std::string padded = data;
        padded.append((4 - padded.size() % 4) % 4, '\0');
        body += padded;
        offset += static_cast<uint32_t>(padded.size());
    }
    std::string out = font.data + body;
    const uint32_t adjustment = 0xB1B0AFBAu - checksum(out);
    for (int i = 0; i < 4; ++i) {
        out[headAt + 8 + static_cast<size_t>(i)] = static_cast<char>((adjustment >> (24 - 8 * i)) & 0xFF);
    }
    return out;
}
}  // namespace

const std::string& glyphlessFont() {
    static const std::string font = makeFont();
    return font;
}

const std::string& cidToGidMap() {
    static const std::string map = [] {
        std::string m;
        m.reserve(2 * 65536);
        for (int i = 0; i < 65536; ++i) {
            m += '\0';
            m += '\1';
        }
        return m;
    }();
    return map;
}

const std::string& toUnicode() {
    static const std::string cmap = [] {
        std::string s = "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
                        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
                        "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
                        "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
        // Every code is its own UTF-16 unit (not the surrogates: no text has them), 100 ranges a block at most
        std::vector<int> highs;
        for (int hi = 0; hi < 256; ++hi) {
            if (hi < 0xD8 || hi > 0xDF) {
                highs.push_back(hi);
            }
        }
        for (size_t i = 0; i < highs.size(); i += 100) {
            const size_t count = std::min<size_t>(100, highs.size() - i);
            s += std::to_string(count) + " beginbfrange\n";
            for (size_t k = i; k < i + count; ++k) {
                char line[40];
                std::snprintf(line, sizeof line, "<%02X00> <%02XFF> <%02X00>\n", highs[k], highs[k], highs[k]);
                s += line;
            }
            s += "endbfrange\n";
        }
        s += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
        return s;
    }();
    return cmap;
}

}  // namespace xqt::InkTextLayer
