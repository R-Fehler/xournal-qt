#include "PdfKeywords.h"

#include <regex>
#include <string>

#include <QRegularExpression>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include "ArchivePdf.h"
#include "IncrementalPdf.h"
#include "PdfEncryption.h"
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

Keywords read(const fs::path& pdf, bool session) {
    Keywords k;
    try {
        QPDF q;
        q.setSuppressWarnings(true);
        if (session) {
            PdfEncryption::openQpdf(q, pdf);
        } else {
            q.processFile(pdf.string().c_str());
        }
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

QString keywordsFor(const QString& old, const QStringList& wanted) {
    QStringList out, covered;
    const bool listed = old.contains(u',') || old.contains(u';');
    static const QRegularExpression lists(QStringLiteral("[,;]"));
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    for (const QString& piece: old.split(listed ? lists : spaces, Qt::SkipEmptyParts)) {
        const QString tag = tags::fromKeyword(piece);
        if (!tag.isEmpty() && tags::contains(wanted, tag) && !tags::contains(covered, tag)) {
            out << piece.trimmed();
            covered << tag;
        }
    }
    for (const QString& t: wanted) {
        if (!t.isEmpty() && !tags::contains(covered, t)) {
            out << t;
            covered << t;
        }
    }
    return out.join(QStringLiteral(", "));
}

namespace {
std::string xmlEscaped(const QString& s) {
    QString e = s.toHtmlEscaped();  // (& < > ")
    e.replace(u'\'', QLatin1String("&apos;"));
    return e.toStdString();
}

/// An XMP packet with its dc:subject and pdf:Keywords set to these (only those that are there are changed; a
/// subject is added beside a pdf:Keywords). Empty when nothing is to change.
/// A text as a regex_replace format ("$" written "$$")
std::string literal(const std::string& s) {
    std::string out;
    for (const char c: s) {
        out += c;
        if (c == '$') {
            out += '$';
        }
    }
    return out;
}

std::string withKeywords(const std::string& xmp, const QStringList& tags, const QString& keywords) {
    std::string out = xmp;
    static const std::regex subject(R"(<dc:subject\b[^>]*>[\s\S]*?</dc:subject\s*>|<dc:subject\b[^>]*/>)");
    static const std::regex keywordsElement(R"(<pdf:Keywords\b[^>]*>[\s\S]*?</pdf:Keywords\s*>|<pdf:Keywords\b[^>]*/>)");
    static const std::regex keywordsAttribute(R"(\bpdf:Keywords\s*=\s*("[^"]*"|'[^']*'))");
    std::string bag = "<dc:subject><rdf:Bag>";
    for (const QString& t: tags) {
        bag += "<rdf:li>" + xmlEscaped(t) + "</rdf:li>";
    }
    bag += "</rdf:Bag></dc:subject>";
    const bool hadSubject = std::regex_search(out, subject);
    const bool hadKeywords = std::regex_search(out, keywordsElement) || std::regex_search(out, keywordsAttribute);
    if (!hadSubject && !hadKeywords) {
        return {};  // (metadata without keywords: the document information is enough)
    }
    if (hadSubject) {
        out = std::regex_replace(out, subject, tags.isEmpty() ? std::string() : literal(bag));
    }
    const std::string k = xmlEscaped(keywords);
    out = std::regex_replace(out, keywordsElement, keywords.isEmpty() ? std::string() : literal("<pdf:Keywords>" + k + "</pdf:Keywords>"));
    out = std::regex_replace(out, keywordsAttribute, literal("pdf:Keywords=\"" + k + "\""));
    return out == xmp ? std::string() : out;
}
}  // namespace

bool write(const fs::path& pdf, const QStringList& tags, std::string& error) {
    try {
        IncrementalPdf::Tail tail;
        if (!IncrementalPdf::readTail(pdf, tail, error)) {
            return false;
        }
        QPDF q;
        q.setSuppressWarnings(true);
        q.processFile(pdf.string().c_str());
        if (q.isEncrypted()) {
            error = "the PDF is encrypted";
            return false;
        }
        IncrementalPdf::Update u(q);
        OH trailer = q.getTrailer();
        OH info = trailer.getKey("/Info");
        QString old;
        if (info.isDictionary() && info.getKey("/Keywords").isString()) {
            old = QString::fromStdString(info.getKey("/Keywords").getUTF8Value());
        }
        const QString keywords = keywordsFor(old, tags);
        if (!info.isDictionary()) {
            info = u.add(OH::newDictionary());
            trailer.replaceKey("/Info", info);
        } else if (!info.isIndirect()) {
            info = u.add(info.shallowCopy());  // (the update's trailer refers to it)
            trailer.replaceKey("/Info", info);
        } else {
            u.touch(info);
        }
        if (keywords.isEmpty()) {
            info.removeKey("/Keywords");
        } else {
            info.replaceKey("/Keywords", OH::newUnicodeString(keywords.toStdString()));
        }
        OH meta = q.getRoot().getKey("/Metadata");
        if (meta.isStream()) {
            auto buffer = meta.getStreamData(qpdf_dl_all);
            const std::string xmp(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
            if (xmp.find("pdfaid:part") != std::string::npos) {
                ArchivePdf::update(q, u);  // (PDF/A: the metadata follows the document information)
            } else if (const std::string changed = withKeywords(xmp, tags::fromKeywords(keywords), keywords);
                       !changed.empty()) {
                u.touch(meta);
                u.touchData(meta);
                meta.replaceStreamData(changed, OH::newNull(), OH::newNull());
            }
        }
        const std::string bytes = u.serialize(tail);
        const IncrementalPdf::Result r = IncrementalPdf::append(pdf, tail, bytes);
        if (!r.ok) {
            error = r.error;
        }
        return r.ok;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

}  // namespace xqt::pdfkeywords
