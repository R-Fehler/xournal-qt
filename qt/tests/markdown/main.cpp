/*
 * xournal-qt: test runner for the Markdown boxes (Qt-free, like the renderer).
 *
 * @license GNU GPLv2 or later
 */
#include <clocale>

#include <gtest/gtest.h>

int main(int argc, char* argv[]) {
    setlocale(LC_NUMERIC, "C");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
