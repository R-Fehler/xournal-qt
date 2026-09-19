/*
 * Xournal++
 *
 * Types of background export
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

/**
 *  @brief List of types for the export of background components.
 *  The order must agree with the corresponding listBackgroundType in ui/exportSettings.glade.
 *  It is constructed so that one can check for intermediate types using comparison.
 */
enum ExportBackgroundType { EXPORT_BACKGROUND_NONE, EXPORT_BACKGROUND_UNRULED, EXPORT_BACKGROUND_ALL };
