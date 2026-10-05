#include "PdfKeywords.h"

#include <regex>
#include <string>

#include <QRegularExpression>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include "Tags.h"

namespace xqt::pdfkeywords {

namespace {
using OH = QPDFObjectHandle;

/// XML's entities decoded (&amp; &lt; &gt; &quot; &apos; &#n; &#xn;)
QString unescaped(const std::string& s) {
    QString out = QString::fromUtf8(s);
    static const QRegularExpression numeric(QStringLiteral("&#(x?)([0-9a-fA-F]+);"));
    QString result;
    qsizetype last = 0;
    for (auto it = numeric.globalMatch(out); it.hasNext();) {
        const auto m = it.next();
        result += out.mid(last, m.capturedStart() - last);
        bool ok = false;
        const uint code = m.captured(2).toUInt(&ok, m.captured(1).isEmpty() ? 10 : 16);
        if (ok && code > 0 && code < 0x110000) {
            const char32_t c = code;
            result += QString::fromUcs4(&c, 1);
        }
        last = m.capturedEnd();
    }
    result += out.mid(last);
    result.replace(QLatin1String("&lt;"), QLatin1String("<"))
            .replace(QLatin1String("&gt;"), QLatin1String(">"))
            .replace(QLatin1String("&quot;"), QLatin1String("\""))
            .replace(QLatin1String("&apos;"), QLatin1String("'"))
            .replace(QLatin1String("&amp;"), QLatin1String("&"));
    return result;
}

/// The entries of the XMP packet's dc:subject (a bag of texts)
QStringList subjectOf(const std::string& xmp) {
    QStringList out;
    static const std::regex subject(R"(<dc:subject\b[^>]*>([\s\S]*?)</dc:subject\s*>)");
    static const std::regex entry(R"(<rdf:li\b[^>]*>([^<]*)</rdf:li\s*>)");
    std::smatch m;
    if (!std::regex_search(xmp, m, subject)) {
        return out;
    }
    const std::string bag = m[1].str();
    for (auto it = std::sregex_iterator(bag.begin(), bag.end(), entry); it != std::sregex_iterator(); ++it) {
        if (const QString v = unescaped((*it)[1].str()).trimmed(); !v.isEmpty()) {
            out << v;
        }
    }
    return out;
}
}  // namespace

QStringList Keywords::tags() const {
    QStringList out = tags::fromKeywords(info);
    for (const QString& s: subject) {
        // (an entry is one keyword, unless it lists several)
        const bool several = s.contains(u',') || s.contains(u';');
        tags::merge(out, several ? tags::fromKeywords(s) : QStringList{tags::fromKeyword(s)});
    }
    return out;
}

Keywords read(const fs::path& pdf) {
    Keywords k;
    try {
        QPDF q;
        q.setSuppressWarnings(true);
        q.processFile(pdf.string().c_str());
        OH info = q.getTrailer().getKey("/Info");
        if (info.isDictionary()) {
            if (OH v = info.getKey("/Keywords"); v.isString()) {
                k.info = QString::fromStdString(v.getUTF8Value());
            }
        }
        OH meta = q.getRoot().getKey("/Metadata");
        if (meta.isStream()) {
            auto buffer = meta.getStreamData(qpdf_dl_all);
            const std::string xmp(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
            k.subject = subjectOf(xmp);
        }
        k.read = true;
    } catch (const std::exception&) {
        // (not a PDF qpdf can read: no keywords)
    }
    return k;
}

QStringList tagsOf(const fs::path& pdf) { return read(pdf).tags(); }

}  // namespace xqt::pdfkeywords
