/*
 * xournal-qt: reading the small JSON files the shell keeps (library settings, reading positions, the recent list,
 * stickers, the plan of a library move). They are written with fileio::writeFileAtomically (session/FileIo.h).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QJsonObject>

#include "filesystem.h"

namespace xqt {

/// The JSON object `file` holds ({} when it does not exist, cannot be read or holds no object). Any thread.
QJsonObject readJsonObject(const fs::path& file);

}  // namespace xqt
