/*
 * xournal-qt: test runner for the upstream Xournal++ unit tests compiled against the Qt-free core.
 * Same environment setup as upstream test/main.cpp (XournalMain::initLocalisation), without GTK.
 *
 * @license GNU GPLv2 or later
 */
#include <clocale>
#include <iostream>
#include <locale>

#include <gtest/gtest.h>
#include <libintl.h>

#include "util/PathUtil.h"
#include "util/StringUtils.h"

#include "config.h"
#include "config-test.h"

class TestEnvironment: public ::testing::Environment {
public:
    void SetUp() override {
        std::cout << "Setting up localisation for tests" << std::endl;
#ifdef ENABLE_NLS
        fs::path localeDir = Util::getGettextFilepath(Util::getLocalePath());
        bindtextdomain(GETTEXT_PACKAGE, char_cast(localeDir.u8string().c_str()));
        textdomain(GETTEXT_PACKAGE);
        bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
#endif
        try {
            std::locale::global(std::locale(""));
        } catch (const std::runtime_error&) {}
        setlocale(LC_NUMERIC, "C");
        std::cout.imbue(std::locale());
    }
};

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment);
    return RUN_ALL_TESTS();
}
