/*
 * xournal-qt: a plugin's modules may import only files inside its folder (qt/docs/decisions/0008-js-plugins.md).
 *
 * QJSEngine resolves `import` statements on the file system and has no hook to refuse one, so the modules are read
 * before the engine loads them: a small tokenizer (comments, strings, template literals and regular expressions
 * skipped as such) finds every static `import … from "x"`, `import "x"` and `export … from "x"`, resolves `x` against
 * the importing file and checks that it lies inside the plugin's folder (links resolved), then reads that module the
 * same way. `import(` (a dynamic import, whose target is computed) is refused. The API module "xournal" is the one
 * name that is not a file.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QString>
#include <QStringList>

namespace xqt::plugins {

/// The module specifiers a source imports (static ones); `dynamic`: it has `import(`
QStringList importsOf(const QString& source, bool* dynamic = nullptr);

/// Checks `main` (a path relative to `folder`) and every module it reaches. "" when all is well, else why not
/// ("main.mjs: imports ../x.mjs, outside the plugin's folder").
QString checkImports(const QString& folder, const QString& main);

}  // namespace xqt::plugins
