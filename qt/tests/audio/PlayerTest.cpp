/*
 * xournal-qt: playing (qt/src/audio/Player.h) through the fake speaker, moved on by hand.
 */
#include <cmath>
#include <filesystem>
#include <numbers>
#include <vector>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "audio/FakeAudio.h"
#include "audio/OggVorbis.h"
#include "audio/Player.h"

using namespace xqt::audio;
namespace fs = std::filesystem;

namespace {
class PlayerTest: public ::testing::Test {
protected:
    void SetUp() override {
        useFakeDevices(true);
        fake::reset();
        fake::config().timers = false;
        // 2 s: silence, with a loud second half
        file = fs::path(dir.filePath("two.ogg").toStdString());
        VorbisWriter w;
        ASSERT_TRUE(w.open(file, 48000));
        std::vector<float> v(96000, 0.f);
        for (size_t i = 48000; i < v.size(); ++i) {
            v[i] = 0.5f * static_cast<float>(std::sin(2 * std::numbers::pi * 440 * static_cast<double>(i) / 48000));
        }
        w.write(v.data(), v.size());
        ASSERT_TRUE(w.close());
    }
    void TearDown() override {
        fake::reset();
        useFakeDevices(false);
    }
    static float peakOf(const std::vector<float>& v) {
        float p = 0;
        for (float x: v) {
            p = std::max(p, std::abs(x));
        }
        return p;
    }
    QTemporaryDir dir;
    fs::path file;
};
}  // namespace

TEST_F(PlayerTest, playsFromThePositionToTheEnd) {
    Player p;
    QSignalSpy finished(&p, &Player::finished);
    ASSERT_TRUE(p.play(file, 500)) << p.error().toStdString();
    EXPECT_EQ(p.state(), Player::State::Playing);
    EXPECT_EQ(p.durationMs(), 2000);
    EXPECT_EQ(p.positionMs(), 500);
    auto heard = fake::play(400);
    EXPECT_EQ(heard.size(), 400u * 48);
    EXPECT_LT(peakOf(heard), 0.05f);  // (the silent half)
    EXPECT_EQ(p.positionMs(), 900);
    fake::play(200);
    heard = fake::play(400);
    EXPECT_NEAR(peakOf(heard), 0.5f, 0.05f);  // (the loud half: what is heard is the file at the position)
    EXPECT_EQ(p.positionMs(), 1500);
    heard = fake::play(1000);
    EXPECT_EQ(heard.size(), 500u * 48);  // (the rest)
    ASSERT_TRUE(finished.wait(1000));
    EXPECT_EQ(p.state(), Player::State::Stopped);
    EXPECT_FALSE(fake::outputRunning());
}

TEST_F(PlayerTest, pauseResumeAndSeek) {
    Player p;
    ASSERT_TRUE(p.play(file));
    fake::play(300);
    p.pause();
    EXPECT_EQ(p.state(), Player::State::Paused);
    EXPECT_TRUE(fake::play(100).empty());
    EXPECT_EQ(p.positionMs(), 300);
    p.seek(1200);  // (while paused: stays paused)
    EXPECT_EQ(p.state(), Player::State::Paused);
    EXPECT_EQ(p.positionMs(), 1200);
    EXPECT_TRUE(fake::play(100).empty());
    p.resume();
    const auto heard = fake::play(100);
    EXPECT_NEAR(peakOf(heard), 0.5f, 0.05f);
    EXPECT_EQ(p.positionMs(), 1300);
    p.seek(100);  // (back)
    EXPECT_EQ(p.positionMs(), 100);
    EXPECT_LT(peakOf(fake::play(100)), 0.05f);
    p.seek(99999);  // (clamped)
    EXPECT_EQ(p.positionMs(), 2000);
    p.stop();
    EXPECT_EQ(p.state(), Player::State::Stopped);
}

TEST_F(PlayerTest, whatCannotBePlayed) {
    Player p;
    EXPECT_FALSE(p.play(fs::path(dir.filePath("missing.ogg").toStdString())));
    EXPECT_FALSE(p.error().isEmpty());
    EXPECT_EQ(p.state(), Player::State::Stopped);
    fake::config().failOutput = true;
    EXPECT_FALSE(p.play(file));
    EXPECT_FALSE(p.error().isEmpty());
    EXPECT_EQ(p.state(), Player::State::Stopped);
}

TEST_F(PlayerTest, anUnpluggedSpeakerStops) {
    Player p;
    QSignalSpy failed(&p, &Player::failed);
    ASSERT_TRUE(p.play(file));
    fake::play(100);
    fake::failOutputNow();
    ASSERT_TRUE(failed.wait(1000));
    EXPECT_EQ(p.state(), Player::State::Stopped);
}

TEST_F(PlayerTest, theTimerDrivesTheFakeSpeaker) {
    fake::config().timers = true;
    Player p;
    QSignalSpy finished(&p, &Player::finished);
    ASSERT_TRUE(p.play(file, 1700));
    ASSERT_TRUE(finished.wait(3000));
}
