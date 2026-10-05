#include "InkTextStore.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include <QCborArray>
#include <QCborValue>

namespace xqt {

const QString InkTextStore::PACK = QStringLiteral("ink-text");

namespace {
qint64 tenths(double v) { return static_cast<qint64>(std::lround(v * 10)); }
double fromTenths(const QCborValue& v) { return static_cast<double>(v.toInteger()) / 10.0; }
qint64 bytes255(float v) { return std::clamp<qint64>(std::lround(v * 255), 0, 255); }
float from255(const QCborValue& v) { return static_cast<float>(v.toInteger()) / 255.0f; }
qint64 asInt(quint64 hash) { return static_cast<qint64>(hash); }
quint64 asHash(const QCborValue& v) { return static_cast<quint64>(v.toInteger()); }
}  // namespace

void InkDoc::assemble() {
    texts.clear();
    texts.reserve(pages.size());
    for (const auto& lines: pages) {
        std::vector<ink::PlacedLine> placed;
        for (const hwr::LineRef& l: lines) {
            placed.push_back({l.origin, l.result});
        }
        auto text = ink::PageText::assemble(placed);
        texts.push_back(text->empty() ? nullptr : text);
    }
}

size_t InkDoc::bytes() const {
    size_t b = sizeof(InkDoc);
    std::set<const ink::LineResult*> seen;
    for (const auto& lines: pages) {
        b += lines.capacity() * sizeof(hwr::LineRef);
        for (const hwr::LineRef& l: lines) {
            if (l.result && seen.insert(l.result.get()).second) {
                b += l.result->bytes();
            }
        }
    }
    for (const auto& t: texts) {
        b += t ? t->bytes() : 0;
    }
    return b;
}

QCborMap InkTextStore::encode(const InkDoc& doc) {
    QCborArray pages;
    std::unordered_map<quint64, std::shared_ptr<const ink::LineResult>> lines;
    for (const auto& page: doc.pages) {
        QCborArray refs;
        for (const hwr::LineRef& l: page) {
            refs.append(QCborArray{asInt(l.hash), tenths(l.origin.x()), tenths(l.origin.y())});
            if (l.result) {
                lines.emplace(l.hash, l.result);
            }
        }
        pages.append(refs);
    }
    QCborArray results;
    for (const auto& [hash, result]: lines) {
        QCborArray line{asInt(hash)};
        for (const ink::Word& w: result->words) {
            QCborArray word{tenths(w.box.x()), tenths(w.box.y()), tenths(w.box.width()), tenths(w.box.height()),
                            bytes255(w.conf)};
            for (size_t i = 0; i < w.candidates.size(); ++i) {
                // (the best reading as recognised, the others as their letters: what the search matches; the share
                // with the models that read it above its 8 bits)
                word.append(i == 0 ? w.text : words::textOf(w.candidates[i].word));
                word.append(bytes255(w.candidates[i].p) | (static_cast<qint64>(w.candidates[i].models) << 8));
            }
            line.append(word);
        }
        if (result->models != 0) {
            line.append(static_cast<qint64>(result->models));  // (the models that read the line: not a word)
        }
        results.append(line);
    }
    return QCborMap{{QStringLiteral("stamp"), doc.stamp},
                    {QStringLiteral("rec"), doc.recognizer},
                    {QStringLiteral("complete"), doc.complete},
                    {QStringLiteral("pages"), pages},
                    {QStringLiteral("lines"), results}};
}

std::shared_ptr<InkDoc> InkTextStore::decode(const QCborMap& map) {
    if (!map.value(QStringLiteral("stamp")).isString() || !map.value(QStringLiteral("pages")).isArray()) {
        return nullptr;
    }
    auto doc = std::make_shared<InkDoc>();
    doc->stamp = map.value(QStringLiteral("stamp")).toString();
    doc->recognizer = map.value(QStringLiteral("rec")).toString();
    doc->complete = map.value(QStringLiteral("complete")).toBool();
    std::unordered_map<quint64, std::shared_ptr<const ink::LineResult>> lines;
    for (const QCborValue& lv: map.value(QStringLiteral("lines")).toArray()) {
        const QCborArray line = lv.toArray();
        if (line.isEmpty()) {
            continue;
        }
        auto result = std::make_shared<ink::LineResult>();
        for (qsizetype k = 1; k < line.size(); ++k) {
            if (line.at(k).isInteger()) {
                result->models = static_cast<uint32_t>(line.at(k).toInteger());
                continue;
            }
            const QCborArray word = line.at(k).toArray();
            if (word.size() < 7) {
                continue;
            }
            ink::Word w;
            w.box = QRectF(fromTenths(word.at(0)), fromTenths(word.at(1)), fromTenths(word.at(2)), fromTenths(word.at(3)));
            w.conf = from255(word.at(4));
            for (qsizetype i = 5; i + 1 < word.size(); i += 2) {
                const QString text = word.at(i).toString();
                if (i == 5) {
                    w.text = text;
                }
                const qint64 v = word.at(i + 1).toInteger();
                ink::Candidate c = ink::candidate(text, static_cast<float>(v & 0xff) / 255.0f);
                c.models = static_cast<uint8_t>((v >> 8) & 0xff);
                w.candidates.push_back(c);
            }
            result->words.push_back(std::move(w));
        }
        lines.emplace(asHash(line.at(0)), std::move(result));
    }
    for (const QCborValue& pv: map.value(QStringLiteral("pages")).toArray()) {
        std::vector<hwr::LineRef> refs;
        for (const QCborValue& rv: pv.toArray()) {
            const QCborArray r = rv.toArray();
            if (r.size() < 3) {
                continue;
            }
            hwr::LineRef ref;
            ref.hash = asHash(r.at(0));
            ref.origin = QPointF(fromTenths(r.at(1)), fromTenths(r.at(2)));
            if (auto it = lines.find(ref.hash); it != lines.end()) {
                ref.result = it->second;
            }
            refs.push_back(std::move(ref));
        }
        doc->pages.push_back(std::move(refs));
    }
    doc->assemble();
    return doc;
}

// --- the store -----------------------------------------------------------------------------------------------------

InkTextStore::InkTextStore(CacheLocation location, QObject* parent): QObject(parent), where(std::move(location)) {
    scheduler = std::make_unique<WriteScheduler>([this] { writeChanged(); }, this);
}

InkTextStore::~InkTextStore() { writeChanged(); }

void InkTextStore::load(const fs::path& folder) {
    {
        std::lock_guard lock(mtx);
        if (folders[folder].loaded) {
            return;
        }
    }
    // Where the library keeps it; else where it may have been kept before
    std::optional<QCborMap> entries;
    for (const fs::path& dir: {where.dirOf(folder), where.inFolder(folder), where.mirrorOf(folder)}) {
        if (!dir.empty() && (entries = Packs::read(dir, PACK, FORMAT))) {
            break;
        }
    }
    std::map<std::string, std::shared_ptr<const InkDoc>> docs;
    if (entries) {
        for (auto it = entries->cbegin(); it != entries->cend(); ++it) {
            if (auto doc = decode(it.value().toMap())) {
                docs.emplace(it.key().toString().toStdString(), std::move(doc));
            }
        }
    }
    std::lock_guard lock(mtx);
    Folder& f = folders[folder];
    if (!f.loaded) {
        // (entries put meanwhile stay)
        for (auto& [name, doc]: docs) {
            f.docs.emplace(name, std::move(doc));
        }
        f.loaded = true;
    }
}

std::shared_ptr<const InkDoc> InkTextStore::find(const fs::path& file) const {
    std::lock_guard lock(mtx);
    auto f = folders.find(file.parent_path());
    if (f == folders.end()) {
        return nullptr;
    }
    auto it = f->second.docs.find(file.filename().string());
    return it != f->second.docs.end() ? it->second : nullptr;
}

void InkTextStore::put(const fs::path& file, InkDoc doc) {
    load(file.parent_path());
    if (doc.texts.size() != doc.pages.size()) {
        doc.assemble();
    }
    {
        std::lock_guard lock(mtx);
        if (discarded) {
            return;
        }
        Folder& f = folders[file.parent_path()];
        f.docs[file.filename().string()] = std::make_shared<const InkDoc>(std::move(doc));
        f.changed = true;
    }
    scheduler->changed();
}

void InkTextStore::erase(const fs::path& file) {
    {
        std::lock_guard lock(mtx);
        auto f = folders.find(file.parent_path());
        if (f == folders.end() || f->second.docs.erase(file.filename().string()) == 0) {
            return;
        }
        f->second.changed = true;
    }
    scheduler->changed();
}

void InkTextStore::moved(const fs::path& from, const fs::path& to) {
    load(from.parent_path());
    load(to.parent_path());
    {
        std::lock_guard lock(mtx);
        Folder& a = folders[from.parent_path()];
        auto it = a.docs.find(from.filename().string());
        if (it == a.docs.end()) {
            return;
        }
        auto doc = it->second;
        a.docs.erase(it);
        a.changed = true;
        Folder& b = folders[to.parent_path()];
        b.docs[to.filename().string()] = std::move(doc);
        b.changed = true;
    }
    scheduler->changed();
}

void InkTextStore::flush() {
    scheduler->cancel();
    writeChanged();
}

void InkTextStore::discard() {
    std::lock_guard lock(mtx);
    discarded = true;
    folders.clear();
}

void InkTextStore::setWriteDelays(int quietMs, int maxDelayMs) { scheduler->setDelays(quietMs, maxDelayMs); }

size_t InkTextStore::bytes() const {
    std::lock_guard lock(mtx);
    size_t b = 0;
    for (const auto& [path, f]: folders) {
        for (const auto& [name, doc]: f.docs) {
            b += doc->bytes();
        }
    }
    return b;
}

void InkTextStore::writeChanged() {
    std::vector<std::pair<fs::path, QCborMap>> todo;
    {
        std::lock_guard lock(mtx);
        if (discarded) {
            return;
        }
        for (auto& [path, f]: folders) {
            if (!f.changed) {
                continue;
            }
            f.changed = false;
            QCborMap entries;
            for (const auto& [name, doc]: f.docs) {
                entries.insert(QString::fromStdString(name), encode(*doc));
            }
            todo.emplace_back(path, std::move(entries));
        }
    }
    for (const auto& [folder, entries]: todo) {
        Packs::write(where.dirOf(folder), PACK, FORMAT, entries, true);
        ++writes;
    }
}

}  // namespace xqt
