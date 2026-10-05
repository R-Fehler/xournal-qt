/*
 * xournal-qt: the recordings of a document (qt/docs/audio.md, "In the document").
 *
 * As in Xournal++, a stroke or a text can be tied to a moment of a recording: AudioContent's audio filename (`fn`, the
 * recording's bare name, found in the audio folder) and timestamp (`ts`, milliseconds from its start). xournal-qt
 * stamps pen strokes (shapes drawn with the pen too) and new texts while a recording runs for their document, as
 * upstream does.
 *
 * A recording is also tied to the page it was started on, as a **voice memo** of that page: the page attribute
 * xqt-audio="name|name" (XojPage::getAudioMemos, an upstream seam like xqt-bookmark). So a recording without ink
 * belongs somewhere too, and follows its page when pages move. Xournal++ ignores the attribute (and drops it when it
 * saves the file); its strokes keep working there.
 *
 * The functions that take a Document or a page read it: the caller holds the document's lock (shared is enough,
 * unique for removeRecording).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/PageRef.h"
#include "undo/UndoAction.h"

class AudioContent;
class Document;
class Element;
class XojPage;
class QDateTime;

namespace xqt::audio {

/// Upstream's play tool plays what is closer than this to the tap (PlayObject::ACTION_RADIUS, in points).
constexpr double PLAY_RADIUS = 15.;
/// Between the names in the xqt-audio page attribute (not in file names on Windows, never in ours).
constexpr char MEMO_SEPARATOR = '|';

std::vector<std::string> parseMemos(std::string_view attribute);
std::string formatMemos(const std::vector<std::string>& names);
/// The page's voice memos (its recordings' names, in the order they were made).
std::vector<std::string> memosOf(const XojPage& page);

/// The name a new recording gets: the time `now` as Xournal++ names its recordings ("2026-10-04_14-03-22.ogg"), with
/// "-2", "-3" … when `taken` says that name is used.
std::string newRecordingName(const QDateTime& now, const std::function<bool(const std::string&)>& taken = {});

/// The recording of an element (nullptr: it has none, or it is not a stroke or a text).
const AudioContent* audioOf(const Element* e);
/// Its name as written into the file (upstream's fn; "" when none).
std::string nameOf(const AudioContent& audio);
/// Ties an element to a moment of a recording.
void stamp(AudioContent& audio, const std::string& name, size_t ts);

struct Recording {
    std::string name;               ///< as the elements and memos name it (upstream's fn)
    std::vector<size_t> pages;      ///< the pages it is on (a memo or elements), ascending, 0-based
    std::vector<size_t> memoPages;  ///< the pages it is a voice memo of
    size_t elements = 0;            ///< strokes and texts tied to it
    size_t firstTs = 0;             ///< the earliest and latest moments of its elements (0: none)
    size_t lastTs = 0;
};
/// Every recording the document refers to, by name.
std::vector<Recording> recordingsOf(const Document& doc);

/// A moment of a recording: an element tied to it.
struct Moment {
    size_t page = 0;
    size_t ts = 0;
    const Element* element = nullptr;
};
/// The moments of the recording `name` in time order (the ticks on the playback slider; the ink written then).
std::vector<Moment> momentsOf(const Document& doc, const std::string& name);

struct Hit {
    std::string name;
    size_t ts = 0;
    const Element* element = nullptr;
};
/// What the play tool plays at (x, y) on the page: of the visible elements with a recording closer than `radius`, the
/// nearest (upstream plays each of them, one after the other cutting the one before short; the nearest is what it
/// ends up playing when they are of one recording).
std::optional<Hit> hitAt(const XojPage& page, double x, double y, double radius = PLAY_RADIUS);

/// One undo step: the voice memos of a page changed (a recording started there, or removed). `apply` sets them on the
/// page (under the document's lock) and tells the views.
class MemoUndoAction final: public UndoAction {
public:
    using Apply = std::function<void(const PageRef&, const std::string& memos)>;
    MemoUndoAction(PageRef page, std::string before, std::string after, std::string text, Apply apply);
    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::string getText() override { return text; }
    std::vector<PageRef> getPages() override { return {}; }  // (no page's picture changes)

private:
    PageRef target;
    std::string before, after, text;
    Apply apply;
};

/// What removeRecording took away, so it can be put back.
struct Removed {
    struct Stamp {
        AudioContent* audio = nullptr;
        std::string fn;
        size_t ts = 0;
    };
    std::string name;
    std::vector<Stamp> stamps;
    std::vector<std::pair<PageRef, std::string>> memos;  ///< pages and their memos before
    bool empty() const { return stamps.empty() && memos.empty(); }
};
/// Removes the recording `name` from the document: the stamps of its strokes and texts and its memos (the file stays
/// where it is). The caller holds the lock (unique).
Removed removeRecording(Document& doc, const std::string& name);
/// Puts back what removeRecording took (the caller holds the lock, unique).
void restore(const Removed& removed);

/// One undo step: a recording removed from the document. `locked` runs a function under the document's lock (unique)
/// and tells the views afterwards.
class RemoveRecordingUndoAction final: public UndoAction {
public:
    using Locked = std::function<void(const std::function<void()>&)>;
    RemoveRecordingUndoAction(Removed removed, Locked locked);
    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::string getText() override;
    std::vector<PageRef> getPages() override { return {}; }

private:
    Removed removed;
    Locked locked;
};

}  // namespace xqt::audio
