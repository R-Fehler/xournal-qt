/*
 * xournal-qt: fake audio devices (AudioDevice.h) for the tests, and for trying the UI without a microphone
 * (XQT_FAKE_AUDIO=1).
 *
 * The fake microphone gives a tone (or silence); the fake speaker takes samples and counts them. By default both are
 * driven by a 10 ms timer, as real devices; a test can switch the timers off and move them on by hand (record(),
 * play()), so it never waits for the clock.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include "AudioDevice.h"

namespace xqt::audio::fake {

struct Config {
    int inputRate = 48000;
    double toneHz = 440;
    float amplitude = 0.3f;  ///< 0: silence
    bool failInput = false;  ///< the microphone cannot be opened (no permission, no device)
    bool failOutput = false;
    bool timers = true;      ///< false: only record() / play() move them
};
/// The settings of the fakes made from now on (and of the running ones: tone, amplitude).
Config& config();
/// Back to the defaults.
void reset();

/// The running fake microphone gives `ms` of samples now. False if none runs (or it is suspended).
bool record(int ms);
/// The running fake speaker takes `ms` of samples now; returns them (empty if none runs or it is suspended).
std::vector<float> play(int ms);
bool inputRunning();
bool outputRunning();
/// The running fake microphone or speaker fails now (unplugged): its onError is called.
void failInputNow();
void failOutputNow();

std::unique_ptr<AudioInput> makeInput();
std::unique_ptr<AudioOutput> makeOutput();

}  // namespace xqt::audio::fake
