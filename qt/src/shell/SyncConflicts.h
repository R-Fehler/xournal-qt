/*
 * xournal-qt: conflict copies that sync apps leave next to a document when it changed in two places at once.
 *
 * Each app names them its own way (the conflict mark comes before the extension):
 *  - Syncthing: "notes.sync-conflict-20240312-101530-ABCDEFG.xopp"
 *  - Dropbox: "notes (conflicted copy).xopp", "notes (Anna's conflicted copy 2024-03-12).xopp", and "(Case Conflict)",
 *    "(Selective Sync Conflict)", ...
 *  - Nextcloud and ownCloud desktop clients: "notes (conflicted copy 2024-03-12 101530).xopp", translated
 *    ("notes (Konflikt …)", "(copie en conflit …)", ...); older ownCloud: "notes_conflict-20240312-101530.xopp"
 *  - Seafile: "notes (SFConflict anna@example.org 2024-03-12-10-15-30).xopp"
 *  - OneDrive: the computer's name appended, "notes-DESKTOP-AB12CDE.xopp" (Windows' default names only: any other
 *    "-NAME" could be part of a real name)
 * A copy counts as a conflict only when the document it belongs to is in the same folder (the library checks that):
 * "Essay (conflict theory).pdf" alone is a document of its own.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>
#include <string>

namespace xqt::SyncConflicts {

struct Conflict {
    std::string original;  ///< the file name of the document it is a conflict copy of ("notes.xopp")
    std::string app;       ///< who made it: "Syncthing", "Dropbox", "Nextcloud", "ownCloud", "Seafile", "OneDrive", or ""
    std::string when;      ///< when, if the name says it ("2024-03-12 10:15"; "" if not)
};

/// The conflict copy this file name is, if it is one (by its name only).
std::optional<Conflict> parse(const std::string& fileName);

}  // namespace xqt::SyncConflicts
