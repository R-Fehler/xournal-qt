/*
 * xournal-qt: recording (qt/src/audio/Recorder.h) with the fake microphone, moved on by hand.
 */
#include <filesystem>

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "audio/FakeAudio.h"
#include "audio/OggVorbis.h"
#include "audio/Recorder.h"

using namespace xqt::audio;
namespace fs = std::filesystem;

namespace {
class RecorderTest: public ::testing::Test {
protected:
    void SetUp() override {
        useFakeDevices(true);
        fake::reset();
        fake::config().timers = false;
    }
    void TearDown() override {
        fake::reset();
        useFakeDevices(false);
    }
    /// `ms` of sound from the fake microphone, in 10 ms pieces as a device gives them.
    static void record(int ms) {
        for (int i = 0; i < ms; i += 10) {
            fake::record(10);
        }
    }
    fs::path file(const char* name) const { return fs::path(dir.filePath(name).toStdString()); }
    QTemporaryDir dir;
};
}  // namespace

TEST_F(RecorderTest, theFileHoldsWhatTheMicrophoneGave) {
    Recorder r;
    QSignalSpy finished(&r, &Recorder::finished);
    QSignalSpy state(&r, &Recorder::stateChanged);
    const fs::path f = file("audio/2026-10-04_10-00-00.ogg");  // (its folder is made)
    ASSERT_TRUE(r.start(f)) << r.error().toStdString();
    EXPECT_EQ(r.state(), Recorder::State::Recording);
    EXPECT_EQ(state.count(), 1);
    EXPECT_EQ(r.positionMs(), 0);
    EXPECT_EQ(r.sampleRate(), 48000);
    record(1000);
    EXPECT_EQ(r.positionMs(), 1000);  // (the time is the count of samples)
    EXPECT_EQ(r.stop(), 1000);
    EXPECT_EQ(r.state(), Recorder::State::Idle);
    EXPECT_FALSE(fake::inputRunning());
    ASSERT_EQ(finished.count(), 1);
    EXPECT_EQ(finished[0][0].toString().toStdString(), f.string());
    EXPECT_EQ(finished[0][1].toLongLong(), 1000);
    EXPECT_EQ(durationMsOf(f), 1000);
    EXPECT_EQ(r.stop(), 0);  // (nothing runs)
}

TEST_F(RecorderTest, pausedTimeIsNotInTheRecording) {
    Recorder r;
    ASSERT_TRUE(r.start(file("paused.ogg")));
    record(500);
    r.pause();
    EXPECT_EQ(r.state(), Recorder::State::Paused);
    EXPECT_FALSE(fake::record(10));  // (the device is suspended)
    EXPECT_EQ(r.positionMs(), 500);
    r.resume();
    record(300);
    EXPECT_EQ(r.positionMs(), 800);
    r.stop();
    EXPECT_EQ(durationMsOf(file("paused.ogg")), 800);
}

TEST_F(RecorderTest, theLevelAndTheSilenceWarning) {
    Recorder r;
    QSignalSpy silent(&r, &Recorder::silentChanged);
    QSignalSpy position(&r, &Recorder::positionChanged);
    fake::config().amplitude = 0.5f;
    ASSERT_TRUE(r.start(file("level.ogg")));
    record(200);
    EXPECT_NEAR(r.level(), 0.5, 0.01);
    EXPECT_GE(position.count(), 2);
    EXPECT_LE(position.count(), 4);  // (every 100 ms, not every piece)
    r.stop();
    EXPECT_EQ(r.level(), 0);

    // A muted microphone: after 5 s the warning, which goes when sound comes
    fake::config().amplitude = 0;
    ASSERT_TRUE(r.start(file("silent.ogg")));
    record(4900);
    EXPECT_FALSE(r.silent());
    record(200);
    EXPECT_TRUE(r.silent());
    EXPECT_EQ(silent.count(), 1);
    fake::config().amplitude = 0.2f;
    record(50);
    EXPECT_FALSE(r.silent());
    EXPECT_EQ(silent.count(), 2);
    r.stop();
}

TEST_F(RecorderTest, aMicrophoneThatCannotBeOpened) {
    fake::config().failInput = true;
    Recorder r;
    EXPECT_FALSE(r.start(file("none.ogg")));
    EXPECT_FALSE(r.error().isEmpty());
    EXPECT_EQ(r.state(), Recorder::State::Idle);
    EXPECT_FALSE(fs::exists(file("none.ogg")));
}

TEST_F(RecorderTest, anUnpluggedMicrophoneEndsTheRecordingAndKeepsWhatCame) {
    Recorder r;
    QSignalSpy failed(&r, &Recorder::failed);
    QSignalSpy finished(&r, &Recorder::finished);
    ASSERT_TRUE(r.start(file("unplugged.ogg")));
    record(700);
    fake::failInputNow();
    ASSERT_TRUE(failed.wait(1000));
    EXPECT_EQ(r.state(), Recorder::State::Idle);
    EXPECT_EQ(finished.count(), 1);
    EXPECT_FALSE(r.error().isEmpty());
    EXPECT_EQ(durationMsOf(file("unplugged.ogg")), 700);
}

TEST_F(RecorderTest, withoutABackendNothingIsRecorded) {
    useFakeDevices(false);
    if (backend() != Backend::None) {
        GTEST_SKIP() << "this build has an audio backend";
    }
    EXPECT_FALSE(available());
    Recorder r;
    EXPECT_FALSE(r.start(file("none.ogg")));
    EXPECT_FALSE(r.error().isEmpty());
}

TEST_F(RecorderTest, theTimerDrivesTheFakeMicrophone) {
    fake::config().timers = true;
    Recorder r;
    ASSERT_TRUE(r.start(file("timed.ogg")));
    QSignalSpy position(&r, &Recorder::positionChanged);
    while (r.positionMs() < 300) {
        ASSERT_TRUE(position.wait(2000));
    }
    r.stop();
    EXPECT_GE(durationMsOf(file("timed.ogg")), 300);
}

/// `xournal-qt --audio-info` (the CI's smoke tests of the Windows and macOS packages look for "recording: available"):
/// the backend in its first line, and with Qt Multimedia the devices the system has (none in a container is fine).
TEST_F(RecorderTest, theDescriptionSaysWhatRecordingRunsOn) {
    EXPECT_EQ(describe(), "recording: available (fake devices, XQT_FAKE_AUDIO=1)\n");
    useNoDevices(true);
    EXPECT_EQ(describe().rfind("recording: not offered", 0), 0u) << describe();
    useNoDevices(false);
    useFakeDevices(false);
    const std::string d = describe();
    if (builtWithQtMultimedia()) {
        EXPECT_EQ(d.rfind("recording: available (Qt Multimedia, Qt ", 0), 0u) << d;
        EXPECT_NE(d.find("\nmicrophones: "), std::string::npos) << d;
        EXPECT_NE(d.find("\nspeakers: "), std::string::npos) << d;
    } else {
        EXPECT_EQ(d, "recording: not offered (this build has no Qt Multimedia)\n");
    }
}

/// The real microphone (a machine with one; XQT_AUDIO_DEVICE=1): one second of sound.
TEST_F(RecorderTest, theRealMicrophone) {
    useFakeDevices(false);
    if (qEnvironmentVariableIntValue("XQT_AUDIO_DEVICE") != 1 || backend() != Backend::Qt) {
        GTEST_SKIP() << "XQT_AUDIO_DEVICE=1 and a build with Qt Multimedia record from the real microphone";
    }
    Recorder r;
    ASSERT_TRUE(r.start(file("real.ogg"))) << r.error().toStdString();
    QSignalSpy position(&r, &Recorder::positionChanged);
    while (r.positionMs() < 1000) {
        ASSERT_TRUE(position.wait(3000));
    }
    r.stop();
    EXPECT_GE(durationMsOf(file("real.ogg")), 1000);
}
