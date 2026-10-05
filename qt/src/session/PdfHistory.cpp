#include "PdfHistory.h"

#include <algorithm>
#include <set>

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <zlib.h>

namespace xqt::PdfHistory {

std::function<std::time_t()> clock;

std::time_t now() { return clock ? clock() : std::time(nullptr); }

std::string isoUtc(std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string localDay(std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

std::string sha256(const std::string& data) {
    return QCryptographicHash::hash(QByteArray::fromRawData(data.data(), static_cast<qsizetype>(data.size())),
                                    QCryptographicHash::Sha256)
            .toHex()
            .toStdString();
}

std::string gunzip(const std::string& data, bool& ok) {
    ok = false;
    z_stream z{};
    if (inflateInit2(&z, 16 + MAX_WBITS) != Z_OK) {
        return {};
    }
    std::string out;
    char buf[65536];
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    z.avail_in = static_cast<uInt>(data.size());
    int rc = Z_OK;
    while (rc == Z_OK) {
        z.next_out = reinterpret_cast<Bytef*>(buf);
        z.avail_out = sizeof buf;
        rc = inflate(&z, Z_NO_FLUSH);
        out.append(buf, sizeof buf - z.avail_out);
    }
    inflateEnd(&z);
    ok = rc == Z_STREAM_END;
    return out;
}

std::string toJsonLines(const std::vector<Version>& versions) {
    std::string out;
    for (const auto& v: versions) {
        QJsonObject o;
        o["id"] = v.id;
        o["date"] = QString::fromStdString(v.date);
        o["day"] = QString::fromStdString(v.day);
        if (!v.message.empty()) {
            o["msg"] = QString::fromStdString(v.message);
        }
        o["start"] = static_cast<qint64>(v.start);
        if (v.end > 0) {
            o["end"] = static_cast<qint64>(v.end);
        }
        if (!v.sha.empty()) {
            o["sha"] = QString::fromStdString(v.sha);
        }
        o["kind"] = QString::fromStdString(v.kind);
        if (v.base >= 0) {
            o["base"] = v.base;
        }
        o["pages"] = v.pages;
        out += QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString() + "\n";
    }
    return out;
}

std::vector<Version> fromJsonLines(const std::string& text) {
    std::vector<Version> out;
    size_t at = 0;
    while (at < text.size()) {
        size_t nl = text.find('\n', at);
        if (nl == std::string::npos) {
            nl = text.size();
        }
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(text.substr(at, nl - at)));
        at = nl + 1;
        if (!doc.isObject()) {
            continue;
        }
        const QJsonObject o = doc.object();
        Version v;
        v.id = o.value("id").toInt(-1);
        if (v.id < 0) {
            continue;
        }
        v.date = o.value("date").toString().toStdString();
        v.day = o.value("day").toString().toStdString();
        v.message = o.value("msg").toString().toStdString();
        v.start = static_cast<uint64_t>(o.value("start").toInteger(0));
        v.end = static_cast<uint64_t>(o.value("end").toInteger(0));
        v.sha = o.value("sha").toString().toStdString();
        v.kind = o.value("kind").toString(QString::fromLatin1(Kind::FULL)).toStdString();
        v.base = o.value("base").toInt(-1);
        v.pages = o.value("pages").toInt(0);
        out.push_back(std::move(v));
    }
    return out;
}

State stateOf(QPDF& q) {
    State s;
    QPDFObjectHandle marker = q.getRoot().getKey("/XournalQt");
    if (!marker.isDictionary()) {
        return s;
    }
    QPDFObjectHandle h = marker.getKey("/History");
    if (h.isDictionary()) {
        QPDFObjectHandle on = h.getKey("/On");
        s.on = on.isBool() && on.getBoolValue();
        QPDFObjectHandle count = h.getKey("/Count");
        s.count = count.isInteger() ? static_cast<int>(count.getIntValue()) : 0;
    }
    QPDFObjectHandle list = marker.getKey("/Versions");
    if (list.isStream()) {
        try {
            auto buffer = list.getStreamData();
            s.versions = fromJsonLines(std::string(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize()));
        } catch (const std::exception&) {
            // (a list that does not read: no versions)
        }
    }
    return s;
}

Listed list(const fs::path& pdf) {
    Listed l;
    l.chain = PdfRevisions::read(pdf);
    l.size = l.chain.size;
    if (!l.chain.ok()) {
        l.error = l.chain.error;
        return l;
    }
    State s;
    try {
        QPDF q;
        PdfRevisions::open(q, pdf, l.chain.end());
        s = stateOf(q);
    } catch (const std::exception& e) {
        l.error = e.what();
        return l;
    }
    l.on = s.on;
    std::set<uint64_t> ends;
    for (const auto& r: l.chain.revisions) {
        ends.insert(r.end);
    }
    for (size_t k = 0; k < s.versions.size(); ++k) {
        Version v = s.versions[k];
        const bool current = k + 1 == s.versions.size();
        if (current || v.end == 0) {
            // (its end was not known when its own list was written: the first revision end after its start)
            v.end = 0;
            for (const auto& r: l.chain.revisions) {
                if (r.end > v.start && (r.start == v.start || v.id == 0)) {
                    v.end = r.end;
                    break;
                }
            }
        }
        if (v.end == 0 || !ends.count(v.end)) {
            ++l.removed;
            continue;
        }
        l.versions.push_back(std::move(v));
    }
    if (!l.versions.empty()) {
        std::set<uint64_t> ours;
        for (const auto& v: l.versions) {
            ours.insert(v.end);
        }
        for (const auto& r: l.chain.revisions) {
            if (r.end > l.versions.front().end && !ours.count(r.end)) {
                l.others.push_back({r.start, r.end, PdfRevisions::dateOf(pdf, r.end)});
            }
        }
    }
    return l;
}

bool replacesLast(const Listed& listed, const std::string& today) {
    if (listed.versions.empty() || !listed.chain.ok() || listed.chain.garbage) {
        return false;
    }
    const Version& cur = listed.versions.back();
    const auto& last = listed.chain.revisions.back();
    // (a version written in full, at the start of the file, is not cut away: that would be writing the file anew)
    return cur.id > 0 && cur.start > 0 && cur.kind != Kind::RECEIVED && !cur.milestone() && cur.day == today &&
           last.start == cur.start && last.end == cur.end;
}

}  // namespace xqt::PdfHistory
