# Audio recordings (qt/docs/features/audio.md): the Ogg Vorbis codec, vendored (qt/3rdparty/libogg,
# qt/3rdparty/libvorbis), so recordings are written and read the same way on every platform, with no system library and
# no FFmpeg.

# --- libogg 1.3.6 -----------------------------------------------------------------------------------------------------
set(XQT_OGG_DIR "${CMAKE_CURRENT_LIST_DIR}/../3rdparty/libogg")
# Its config_types.h, as its own CMakeLists.txt makes it (the C99 fixed-size types)
set(INCLUDE_INTTYPES_H 1)
set(INCLUDE_STDINT_H 1)
set(INCLUDE_SYS_TYPES_H 1)
set(SIZE16 int16_t)
set(USIZE16 uint16_t)
set(SIZE32 int32_t)
set(USIZE32 uint32_t)
set(SIZE64 int64_t)
set(USIZE64 uint64_t)
configure_file("${XQT_OGG_DIR}/include/ogg/config_types.h.in" "${CMAKE_CURRENT_BINARY_DIR}/libogg/ogg/config_types.h" @ONLY)
add_library(xqt-ogg STATIC
    ${XQT_OGG_DIR}/src/bitwise.c
    ${XQT_OGG_DIR}/src/framing.c)
target_include_directories(xqt-ogg PUBLIC "${XQT_OGG_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/libogg")

# --- libvorbis 1.3.7: the codec, the encoder (vorbisenc) and the file reader (vorbisfile) in one library -------------
set(XQT_VORBIS_DIR "${CMAKE_CURRENT_LIST_DIR}/../3rdparty/libvorbis")
add_library(xqt-vorbis STATIC
    ${XQT_VORBIS_DIR}/lib/mdct.c
    ${XQT_VORBIS_DIR}/lib/smallft.c
    ${XQT_VORBIS_DIR}/lib/block.c
    ${XQT_VORBIS_DIR}/lib/envelope.c
    ${XQT_VORBIS_DIR}/lib/window.c
    ${XQT_VORBIS_DIR}/lib/lsp.c
    ${XQT_VORBIS_DIR}/lib/lpc.c
    ${XQT_VORBIS_DIR}/lib/analysis.c
    ${XQT_VORBIS_DIR}/lib/synthesis.c
    ${XQT_VORBIS_DIR}/lib/psy.c
    ${XQT_VORBIS_DIR}/lib/info.c
    ${XQT_VORBIS_DIR}/lib/floor1.c
    ${XQT_VORBIS_DIR}/lib/floor0.c
    ${XQT_VORBIS_DIR}/lib/res0.c
    ${XQT_VORBIS_DIR}/lib/mapping0.c
    ${XQT_VORBIS_DIR}/lib/registry.c
    ${XQT_VORBIS_DIR}/lib/codebook.c
    ${XQT_VORBIS_DIR}/lib/sharedbook.c
    ${XQT_VORBIS_DIR}/lib/lookup.c
    ${XQT_VORBIS_DIR}/lib/bitrate.c
    ${XQT_VORBIS_DIR}/lib/vorbisenc.c
    ${XQT_VORBIS_DIR}/lib/vorbisfile.c)
target_include_directories(xqt-vorbis PUBLIC "${XQT_VORBIS_DIR}/include" PRIVATE "${XQT_VORBIS_DIR}/lib")
target_link_libraries(xqt-vorbis PUBLIC xqt-ogg)
if(NOT WIN32 AND NOT APPLE)
    target_link_libraries(xqt-vorbis PRIVATE m)
endif()
foreach(_xqt_codec xqt-ogg xqt-vorbis)
    set_target_properties(${_xqt_codec} PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF POSITION_INDEPENDENT_CODE ON
        C_STANDARD 99)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${_xqt_codec} PRIVATE -w)  # (third-party code: its warnings are not ours)
    endif()
endforeach()

# --- the recordings ---------------------------------------------------------------------------------------------------
# xqt-audio: the files (OggVorbis), the recorder and player behind interfaces, the fake devices of the tests.
add_library(xqt-audio STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/audio/OggVorbis.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/audio/OggVorbis.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/audio/AudioDevice.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/audio/AudioDevice.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/audio/FakeAudio.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/audio/FakeAudio.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/audio/Recorder.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/audio/Recorder.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/audio/Player.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/audio/Player.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/audio/DocumentAudio.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/audio/DocumentAudio.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/audio/AudioFiles.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/audio/AudioFiles.cpp)
target_include_directories(xqt-audio PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src")
target_link_libraries(xqt-audio PUBLIC Qt6::Core xoj-core PRIVATE xqt-vorbis)
set_target_properties(xqt-audio PROPERTIES AUTOMOC ON)

# The microphone and the speaker through Qt Multimedia (QAudioSource / QAudioSink; no FFmpeg plugin needed: the files
# are Ogg Vorbis by the codec above, and since Qt 6.5 the audio devices are in the Qt Multimedia library itself, on
# every platform: PulseAudio/PipeWire, WASAPI, Core Audio, AAudio/OpenSL ES). Without it the app builds and runs, and
# does not offer recording; XQT_FAKE_AUDIO=1 then gives fake devices (qt/src/audio/FakeAudio.h) to try the UI.
# The release packages for Windows, macOS and Android configure with XQT_REQUIRE_AUDIO=ON, so that a missing Qt
# Multimedia fails their build instead of shipping without recording (qt/docs/development/releasing.md).
option(XQT_AUDIO "Audio recordings: record and play through Qt Multimedia when it is found" ON)
option(XQT_REQUIRE_AUDIO "Fail when Qt Multimedia is not found (release packages: they offer recording)" OFF)
set(XQT_HAVE_QT_MULTIMEDIA OFF)
if(XQT_AUDIO)
    find_package(Qt6 ${Qt6_VERSION} QUIET COMPONENTS Multimedia)
    if(TARGET Qt6::Multimedia)
        set(XQT_HAVE_QT_MULTIMEDIA ON)
    endif()
endif()
if(XQT_HAVE_QT_MULTIMEDIA)
    message(STATUS "Audio recordings: recording is built (Qt Multimedia ${Qt6Multimedia_VERSION})")
    target_sources(xqt-audio PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../src/audio/QtAudioDevice.cpp)
    target_link_libraries(xqt-audio PUBLIC Qt6::Multimedia)
    target_compile_definitions(xqt-audio PRIVATE XQT_HAVE_QT_MULTIMEDIA)
elseif(XQT_REQUIRE_AUDIO)
    message(FATAL_ERROR "Audio recordings: XQT_REQUIRE_AUDIO is on, but Qt6::Multimedia was not found (XQT_AUDIO="
        "${XQT_AUDIO}). Install Qt Multimedia: qt6-multimedia-dev (Debian, Ubuntu), "
        "mingw-w64-ucrt-x86_64-qt6-multimedia (MSYS2), qtmultimedia (Homebrew), aqt's -m qtmultimedia (Android).")
else()
    message(STATUS "Audio recordings: recording is NOT built (Qt6::Multimedia not found or XQT_AUDIO off): the record "
        "button is hidden; XQT_FAKE_AUDIO=1 uses fake devices")
endif()

if(XQT_BUILD_TESTS)
    add_executable(xqt-audio-tests
        ${CMAKE_CURRENT_LIST_DIR}/../tests/audio/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/audio/OggVorbisTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/audio/RecorderTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/audio/PlayerTest.cpp)
    target_link_libraries(xqt-audio-tests PRIVATE xqt-audio xqt-test-support Qt6::Test GTest::gtest)
    target_include_directories(xqt-audio-tests PRIVATE "${TEST_CONFIG_DIR}")
    gtest_discover_tests(xqt-audio-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS audio
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
