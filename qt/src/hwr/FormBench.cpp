#include "FormBench.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <shared_mutex>

#include <QElapsedTimer>
#include <QJsonArray>
#include <QStringList>

#include "model/Document.h"
#include "model/XojPage.h"
#include "session/InkText.h"
#include "session/TextMatch.h"
#include "session/WordMatch.h"

#include "FormManifest.h"
#include "InkLayout.h"
#include "Recognizer.h"

namespace xqt::hwr {

int editsBetween(const QString& a, const QString& b) {
    std::vector<int> prev(static_cast<size_t>(b.size()) + 1), cur(prev.size());
    for (size_t j = 0; j < prev.size(); ++j) {
        prev[j] = static_cast<int>(j);
    }
    for (qsizetype i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        for (qsizetype j = 1; j <= b.size(); ++j) {
            const auto J = static_cast<size_t>(j);
            cur[J] = std::min({prev[J] + 1, cur[J - 1] + 1, prev[J - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        }
        std::swap(prev, cur);
    }
    return prev.back();
}

namespace {

/// What one recogniser made of one box
struct Reading {
    QString text;  ///< the best readings of its words
    int edits = 0, chars = 0;
    int terms = 0, found = 0;
    int search = 0, searchFound = 0;
    int words = 0;  ///< words read in it (drawing, mark: there should be none)
    int falseTries = 0, falseHits = 0;
};

struct BoxResult {
    const FormItem* item = nullptr;
    bool written = false;
    std::vector<double> lineAngles;  ///< of the layout's lines in it
    std::vector<Reading> readings;   ///< per model
};

/// Sums over boxes
struct Counts {
    int boxes = 0, written = 0, empty = 0;
    int textBoxes = 0, oneLine = 0, lines = 0, anglesRight = 0;
    struct PerModel {
        int edits = 0, chars = 0, terms = 0, found = 0, search = 0, searchFound = 0;
        int nonText = 0, nonTextWords = 0, falseTries = 0, falseHits = 0;
    };
    std::vector<PerModel> models;
};

double angleBetween(double a, double b) {
    double d = std::fmod(a - b, 360.0);
    if (d > 180) {
        d -= 360;
    } else if (d < -180) {
        d += 360;
    }
    return std::abs(d);
}

QJsonValue rate(int a, int b) {
    return b > 0 ? QJsonValue(std::round(10000.0 * a / b) / 10000.0) : QJsonValue(QJsonValue::Null);
}

QString percent(int a, int b) { return b > 0 ? QString::number(100.0 * a / b, 'f', 1) : QStringLiteral("–"); }

QString angleName(double a) { return QString::number(a, 'g', 4); }

/// The words of the page whose middle is in the item's box
std::vector<uint32_t> wordsIn(const ink::PageText& page, const FormItem& item) {
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < page.words.size(); ++i) {
        if (item.contains(page.words[i].box.center())) {
            out.push_back(i);
        }
    }
    return out;
}

/// The plain search's term of a text
textmatch::Term termOf(const QString& text) { return {textmatch::prepare(text), textmatch::Anywhere}; }

/// The item a hit is in (by its first word), or null
const FormItem* itemOf(const ink::PageText& page, const ink::Hit& hit, const std::vector<const FormItem*>& items,
                       const std::vector<bool>& written) {
    const QPointF c = page.words[hit.first].box.center();
    for (size_t i = 0; i < items.size(); ++i) {
        if (written[i] && items[i]->contains(c)) {
            return items[i];
        }
    }
    return nullptr;
}

void add(Counts& c, const BoxResult& b, size_t models) {
    c.models.resize(models);
    ++c.boxes;
    if (!b.written) {
        ++c.empty;
        return;
    }
    ++c.written;
    const bool text = b.item->textual();
    if (text) {
        ++c.textBoxes;
        c.oneLine += b.lineAngles.size() == 1 ? 1 : 0;
        c.lines += static_cast<int>(b.lineAngles.size());
        for (const double a: b.lineAngles) {
            c.anglesRight += angleBetween(a, b.item->angle) <= ANGLE_TOLERANCE ? 1 : 0;
        }
    }
    for (size_t m = 0; m < models && m < b.readings.size(); ++m) {
        const Reading& r = b.readings[m];
        Counts::PerModel& p = c.models[m];
        p.falseTries += r.falseTries;
        p.falseHits += r.falseHits;
        if (text) {
            p.edits += r.edits;
            p.chars += r.chars;
            p.terms += r.terms;
            p.found += r.found;
            p.search += r.search;
            p.searchFound += r.searchFound;
        } else if (b.item->kind != QLatin1String("free")) {
            ++p.nonText;
            p.nonTextWords += r.words;
        }
    }
}

QJsonObject jsonOf(const Counts& c, const std::vector<BenchModel>& models) {
    QJsonObject perModel;
    for (size_t m = 0; m < models.size(); ++m) {
        const Counts::PerModel p = m < c.models.size() ? c.models[m] : Counts::PerModel{};
        perModel.insert(models[m].name,
                        QJsonObject{{QStringLiteral("cer"), rate(p.edits, p.chars)},
                                    {QStringLiteral("chars"), p.chars},
                                    {QStringLiteral("words_found"), rate(p.found, p.terms)},
                                    {QStringLiteral("terms"), p.terms},
                                    {QStringLiteral("search_recall"), rate(p.searchFound, p.search)},
                                    {QStringLiteral("search_words"), p.search},
                                    {QStringLiteral("text_in_drawings"), p.nonTextWords},
                                    {QStringLiteral("drawing_boxes"), p.nonText},
                                    {QStringLiteral("false_hits"), rate(p.falseHits, p.falseTries)},
                                    {QStringLiteral("false_tries"), p.falseTries}});
    }
    return {{QStringLiteral("boxes"), c.boxes},
            {QStringLiteral("written"), c.written},
            {QStringLiteral("empty"), c.empty},
            {QStringLiteral("text_boxes"), c.textBoxes},
            {QStringLiteral("one_line"), rate(c.oneLine, c.textBoxes)},
            {QStringLiteral("lines"), c.lines},
            {QStringLiteral("angle_ok"), rate(c.anglesRight, c.lines)},
            {QStringLiteral("models"), perModel}};
}

}  // namespace

BenchReport runBench(Document& doc, const FormManifest& manifest, const std::vector<BenchModel>& models,
                     const QString& source) {
    BenchReport report;
    if (models.empty()) {
        report.error = QStringLiteral("No recogniser");
        return report;
    }
    std::vector<BoxResult> boxes;
    double layoutMs = 0;
    std::vector<double> modelMs(models.size(), 0);
    std::vector<int> modelLines(models.size(), 0);
    Context context;
    context.language = manifest.language.isEmpty() ? QStringLiteral("en") : manifest.language;
    QElapsedTimer timer;
    std::shared_lock lock(doc);
    for (size_t p = 0; p < doc.getPageCount(); ++p) {
        const XojPage& page = *doc.getPage(p);
        const std::vector<const FormItem*> items = manifest.itemsOn(formPageOf(page, p));
        if (items.empty()) {
            continue;
        }
        const std::vector<InkStroke> strokes = strokesOf(page);
        // The app's pipeline on the whole page
        timer.start();
        const Layout layout = hwr::layout(strokes);
        layoutMs += static_cast<double>(timer.nsecsElapsed()) / 1e6;
        std::vector<std::shared_ptr<const ink::PageText>> texts;
        for (size_t m = 0; m < models.size(); ++m) {
            std::vector<ink::PlacedLine> placed;
            for (const InkLine& line: layout.lines) {
                timer.start();
                auto read = models[m].recognizer->recognizeLine(LineInput::of(strokes, layout, line), context);
                modelMs[m] += static_cast<double>(timer.nsecsElapsed()) / 1e6;
                ++modelLines[m];
                if (read) {
                    placed.push_back(
                            {line.origin(), std::make_shared<const ink::LineResult>(std::move(*read)), line.angle});
                }
            }
            texts.push_back(ink::PageText::assemble(placed));
        }
        // Compared with the manifest
        const std::vector<int> owner = assign(strokes, items);
        std::vector<bool> written(items.size(), false);
        for (const int o: owner) {
            if (o >= 0) {
                written[static_cast<size_t>(o)] = true;
            }
        }
        const size_t first = boxes.size();
        for (size_t b = 0; b < items.size(); ++b) {
            BoxResult r;
            r.item = items[b];
            r.written = written[b];
            r.readings.resize(models.size());
            if (r.written) {
                for (const InkLine& line: layout.lines) {
                    if (r.item->contains(line.box.center())) {
                        r.lineAngles.push_back(line.angle);
                    }
                }
            }
            boxes.push_back(std::move(r));
        }
        // The search words of the page's boxes, each once (the false hits)
        std::vector<QString> pageWords;
        for (size_t b = 0; b < items.size(); ++b) {
            for (const QString& s: items[b]->search) {
                if (const QString t = textmatch::prepare(s); !t.isEmpty() &&
                                                             std::find(pageWords.begin(), pageWords.end(), t) ==
                                                                     pageWords.end()) {
                    pageWords.push_back(t);
                }
            }
        }
        for (size_t m = 0; m < models.size(); ++m) {
            const ink::PageText& text = *texts[m];
            for (size_t b = 0; b < items.size(); ++b) {
                BoxResult& box = boxes[first + b];
                if (!box.written) {
                    continue;
                }
                const FormItem& item = *box.item;
                Reading& r = box.readings[m];
                const std::vector<uint32_t> in = wordsIn(text, item);
                ink::PageText own;
                own.lineStarts.push_back(0);
                QStringList read;
                for (const uint32_t i: in) {
                    own.words.push_back(text.words[i]);
                    if (!text.words[i].text.trimmed().isEmpty()) {
                        read << text.words[i].text.trimmed();
                    }
                }
                r.text = read.join(u' ');
                r.words = static_cast<int>(read.size());
                if (!item.textual()) {
                    continue;
                }
                r.chars = static_cast<int>(item.text.size());
                r.edits = editsBetween(item.text, r.text);
                textmatch::words(item.text, [&](qsizetype, qsizetype, QStringView w) {
                    if (w.size() < wordmatch::MIN_LETTERS) {
                        return;
                    }
                    ++r.terms;
                    r.found += ink::find(own, {termOf(w.toString())}, wordmatch::DEFAULT_TYPOS).empty() ? 0 : 1;
                });
                for (const QString& s: item.search) {
                    ++r.search;
                    for (const ink::Hit& h: ink::find(text, {termOf(s)}, wordmatch::DEFAULT_TYPOS)) {
                        if (itemOf(text, h, {&item}, {true}) != nullptr) {
                            ++r.searchFound;
                            break;
                        }
                    }
                }
            }
            // False hits: a word of another box found in this one
            std::vector<std::set<QString>> hitBy(items.size());
            for (const QString& w: pageWords) {
                for (const ink::Hit& h: ink::find(text, {termOf(w)}, wordmatch::DEFAULT_TYPOS)) {
                    if (const FormItem* in = itemOf(text, h, items, written)) {
                        hitBy[static_cast<size_t>(std::find(items.begin(), items.end(), in) - items.begin())]
                                .insert(w);
                    }
                }
            }
            for (size_t b = 0; b < items.size(); ++b) {
                BoxResult& box = boxes[first + b];
                if (!box.written) {
                    continue;
                }
                const QString own = textmatch::prepare(box.item->text);
                Reading& r = box.readings[m];
                for (const QString& w: pageWords) {
                    if (textmatch::contains(own, w)) {
                        continue;  // (its own word)
                    }
                    ++r.falseTries;
                    r.falseHits += hitBy[b].count(w) ? 1 : 0;
                }
            }
        }
    }

    // Groups
    std::map<QString, Counts> groups;
    for (const BoxResult& b: boxes) {
        const FormItem& i = *b.item;
        QStringList keys{QStringLiteral("all")};
        if (!i.section.isEmpty()) {
            keys << QStringLiteral("section:") + i.section;
        }
        keys << QStringLiteral("kind:") + i.kind;
        if (i.xHeightMm > 0) {
            keys << QStringLiteral("size:%1mm").arg(i.xHeightMm);
        }
        keys << QStringLiteral("angle:") + angleName(i.angle);
        for (const QString& k: keys) {
            add(groups[k], b, models.size());
        }
    }
    QJsonObject groupsJson;
    for (const auto& [key, counts]: groups) {
        groupsJson.insert(key, jsonOf(counts, models));
    }
    QJsonArray modelsJson;
    QJsonObject timing{{QStringLiteral("layout_ms"), std::round(layoutMs * 10) / 10}};
    QJsonObject timingModels;
    for (size_t m = 0; m < models.size(); ++m) {
        modelsJson.append(QJsonObject{{QStringLiteral("name"), models[m].name},
                                      {QStringLiteral("folder"), models[m].folder},
                                      {QStringLiteral("id"), models[m].recognizer->capabilities().id}});
        timingModels.insert(models[m].name,
                            QJsonObject{{QStringLiteral("ms"), std::round(modelMs[m] * 10) / 10},
                                        {QStringLiteral("lines"), modelLines[m]},
                                        {QStringLiteral("ms_per_line"),
                                         modelLines[m] ? std::round(modelMs[m] / modelLines[m] * 10) / 10 : 0.0}});
    }
    timing.insert(QStringLiteral("models"), timingModels);
    QJsonArray boxesJson;
    for (const BoxResult& b: boxes) {
        const FormItem& i = *b.item;
        QJsonArray angles;
        for (const double a: b.lineAngles) {
            angles.append(std::round(a * 10) / 10);
        }
        QJsonObject read;
        for (size_t m = 0; m < models.size(); ++m) {
            const Reading& r = b.readings[m];
            QJsonObject o{{QStringLiteral("text"), r.text}, {QStringLiteral("words"), r.words}};
            if (i.textual()) {
                o.insert(QStringLiteral("cer"), rate(r.edits, r.chars));
                o.insert(QStringLiteral("terms"), r.terms);
                o.insert(QStringLiteral("found"), r.found);
                o.insert(QStringLiteral("search"), r.search);
                o.insert(QStringLiteral("search_found"), r.searchFound);
            }
            o.insert(QStringLiteral("false_tries"), r.falseTries);
            o.insert(QStringLiteral("false_hits"), r.falseHits);
            read.insert(models[m].name, o);
        }
        boxesJson.append(QJsonObject{{QStringLiteral("id"), i.id},
                                     {QStringLiteral("page"), i.page},
                                     {QStringLiteral("section"), i.section},
                                     {QStringLiteral("kind"), i.kind},
                                     {QStringLiteral("text"), i.text},
                                     {QStringLiteral("angle"), i.angle},
                                     {QStringLiteral("x_height_mm"), i.xHeightMm},
                                     {QStringLiteral("written"), b.written},
                                     {QStringLiteral("lines"), static_cast<int>(b.lineAngles.size())},
                                     {QStringLiteral("line_angles"), angles},
                                     {QStringLiteral("read"), read}});
    }
    report.json = QJsonObject{{QStringLiteral("form"), manifest.form},
                              {QStringLiteral("version"), manifest.version},
                              {QStringLiteral("source"), source},
                              {QStringLiteral("models"), modelsJson},
                              {QStringLiteral("timing"), timing},
                              {QStringLiteral("groups"), groupsJson},
                              {QStringLiteral("boxes"), boxesJson}};

    // Markdown: a table per grouping, the models side by side
    QString md;
    md += QStringLiteral("# Handwriting benchmark: %1 v%2\n\n").arg(manifest.form).arg(manifest.version);
    md += QStringLiteral("Source: %1. Models: ").arg(source);
    QStringList names;
    for (const BenchModel& m: models) {
        names << QStringLiteral("%1 (%2)").arg(m.name, m.recognizer->capabilities().id);
    }
    md += names.join(QStringLiteral(", ")) + QStringLiteral(".\n\n");
    md += QStringLiteral("Percentages. one line: boxes for text holding exactly one line of the layout; angle ok: "
                         "lines at the box's angle (±%1°); CER: of the best readings; found: words of 3+ letters "
                         "among the readings (the plain search); recall: the boxes' search words found in them by the "
                         "search over the page; drawn: words read in drawing and mark boxes (a count); false: other "
                         "boxes' words hitting this one.\n")
                  .arg(ANGLE_TOLERANCE);
    const std::vector<std::pair<QString, QString>> sections{{QStringLiteral("Overall"), QStringLiteral("all")},
                                                            {QStringLiteral("By section"), QStringLiteral("section:")},
                                                            {QStringLiteral("By kind"), QStringLiteral("kind:")},
                                                            {QStringLiteral("By size"), QStringLiteral("size:")},
                                                            {QStringLiteral("By angle"), QStringLiteral("angle:")}};
    for (const auto& [title, prefix]: sections) {
        md += QStringLiteral("\n## %1\n\n| group | boxes | written | empty | one line | angle ok |").arg(title);
        QString rule = QStringLiteral("| --- | ---: | ---: | ---: | ---: | ---: |");
        for (const BenchModel& m: models) {
            const QString n = models.size() > 1 ? QStringLiteral(" (%1)").arg(m.name) : QString();
            md += QStringLiteral(" CER%1 | found%1 | recall%1 | drawn%1 | false%1 |").arg(n);
            rule += QStringLiteral(" ---: | ---: | ---: | ---: | ---: |");
        }
        md += u'\n' + rule + u'\n';
        std::vector<std::pair<QString, const Counts*>> rows;
        for (const auto& [key, counts]: groups) {
            if (prefix == QLatin1String("all") ? key == prefix : key.startsWith(prefix)) {
                rows.emplace_back(key.mid(prefix == QLatin1String("all") ? 0 : prefix.size()), &counts);
            }
        }
        if (prefix == QLatin1String("size:") || prefix == QLatin1String("angle:")) {
            std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
                return QStringView(a.first).chopped(a.first.endsWith(QLatin1String("mm")) ? 2 : 0).toDouble() <
                       QStringView(b.first).chopped(b.first.endsWith(QLatin1String("mm")) ? 2 : 0).toDouble();
            });
        }
        for (const auto& [name, c]: rows) {
            md += QStringLiteral("| %1 | %2 | %3 | %4 | %5 | %6 |")
                          .arg(name)
                          .arg(c->boxes)
                          .arg(c->written)
                          .arg(c->empty)
                          .arg(percent(c->oneLine, c->textBoxes), percent(c->anglesRight, c->lines));
            for (size_t m = 0; m < models.size(); ++m) {
                const Counts::PerModel p = m < c->models.size() ? c->models[m] : Counts::PerModel{};
                md += QStringLiteral(" %1 | %2 | %3 | %4 | %5 |")
                              .arg(percent(p.edits, p.chars), percent(p.found, p.terms),
                                   percent(p.searchFound, p.search),
                                   p.nonText > 0 ? QString::number(p.nonTextWords) : QStringLiteral("–"),
                                   percent(p.falseHits, p.falseTries));
            }
            md += u'\n';
        }
    }
    md += QStringLiteral("\n## Timing\n\nLayout: %1 ms.").arg(layoutMs, 0, 'f', 1);
    for (size_t m = 0; m < models.size(); ++m) {
        md += QStringLiteral(" %1: %2 ms for %3 lines (%4 ms a line).")
                      .arg(models[m].name)
                      .arg(modelMs[m], 0, 'f', 0)
                      .arg(modelLines[m])
                      .arg(modelLines[m] ? modelMs[m] / modelLines[m] : 0.0, 0, 'f', 1);
    }
    md += u'\n';
    report.markdown = md;
    report.ok = true;
    return report;
}

}  // namespace xqt::hwr
