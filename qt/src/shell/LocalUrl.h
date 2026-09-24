/*
 * xournal-qt: the local path of a file URL, also on Windows.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QString>
#include <QUrl>

namespace xqt {

/// The local path a URL stands for: QUrl::toLocalFile() for a file URL. Also repairs the two ways a Windows path
/// goes wrong when a URL is made from it by hand (the Windows reports of 2026-09-24, "cannot open c//"):
/// - "file://" + "C:/Users/x" is "file://C:/Users/x", whose host is "c": the drive letter is put back ("C:/Users/x"
///   and not the network path "//c/Users/x" that toLocalFile() gives);
/// - "C:/Users/x" given where a URL is expected has the scheme "c": the drive letter and the path again.
/// Anything else that is not a file URL: its text (a plain path passed as a URL). Make URLs of paths with
/// QUrl::fromLocalFile, never by adding "file://".
QString localPathOf(const QUrl& url);

}  // namespace xqt
