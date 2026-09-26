/*
 * xournal-qt: test runner for the Markdown boxes (Qt-free, like the renderer).
 *
 * @license GNU GPLv2 or later
 */
#include <clocale>

#include <gtest/gtest.h>

#include "EmojiFont.h"

int main(int argc, char* argv[]) {
    setlocale(LC_NUMERIC, "C");
    // The app's colour emoji font, as the app has it (AppContext)
    xqt::emoji::registerFont(XQT_EMOJI_FONT);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
