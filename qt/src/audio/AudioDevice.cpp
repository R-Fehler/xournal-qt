#include "AudioDevice.h"

#include <atomic>

#include <QtGlobal>

#include "FakeAudio.h"

namespace xqt::audio {

#ifdef XQT_HAVE_QT_MULTIMEDIA
// QtAudioDevice.cpp
std::unique_ptr<AudioInput> makeQtInput();
std::unique_ptr<AudioOutput> makeQtOutput();
#endif

namespace {
std::atomic<int> forced{-1};  ///< -1: by the environment; else a Backend
}

bool builtWithQtMultimedia() {
#ifdef XQT_HAVE_QT_MULTIMEDIA
    return true;
#else
    return false;
#endif
}

Backend backend() {
    if (const int f = forced.load(); f >= 0) {
        return static_cast<Backend>(f);
    }
    if (qEnvironmentVariableIntValue("XQT_FAKE_AUDIO") == 1) {
        return Backend::Fake;
    }
    return builtWithQtMultimedia() ? Backend::Qt : Backend::None;
}

void useFakeDevices(bool on) { forced = on ? static_cast<int>(Backend::Fake) : -1; }

void useNoDevices(bool on) { forced = on ? static_cast<int>(Backend::None) : -1; }

std::unique_ptr<AudioInput> makeInput() {
    switch (backend()) {
        case Backend::Fake:
            return fake::makeInput();
        case Backend::Qt:
#ifdef XQT_HAVE_QT_MULTIMEDIA
            return makeQtInput();
#endif
        case Backend::None:
            break;
    }
    return nullptr;
}

std::unique_ptr<AudioOutput> makeOutput() {
    switch (backend()) {
        case Backend::Fake:
            return fake::makeOutput();
        case Backend::Qt:
#ifdef XQT_HAVE_QT_MULTIMEDIA
            return makeQtOutput();
#endif
        case Backend::None:
            break;
    }
    return nullptr;
}

}  // namespace xqt::audio
