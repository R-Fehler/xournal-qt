# The Android package: an APK built by androiddeployqt from the xournal-qt target (see ../docs/android.md).
#   cmake --preset android-arm64-debug && cmake --build build-android --target apk

qt_policy(SET QTP0002 NEW)  # Android paths in target properties may be generator expressions
set(XQT_ANDROID_DIR "${CMAKE_CURRENT_LIST_DIR}/../packaging/android")

target_sources(xournal-qt PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/AndroidSetup.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/AndroidSetup.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/AndroidActivity.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/AndroidActivity.cpp)

# The resources the core reads as files (page templates, palettes, icons) travel as Qt resources and are copied to
# the app's data folder at start (AndroidSetup.cpp).
file(GLOB_RECURSE _xqt_share_files LIST_DIRECTORIES false RELATIVE "${XQT_BUILD_RESOURCE_DIR}" "${XQT_BUILD_RESOURCE_DIR}/*")
set(_xqt_share_abs)
foreach(f ${_xqt_share_files})
    list(APPEND _xqt_share_abs "${XQT_BUILD_RESOURCE_DIR}/${f}")
endforeach()
qt_add_resources(xournal-qt xqt_android_share PREFIX /xqt-share BASE "${XQT_BUILD_RESOURCE_DIR}"
    FILES ${_xqt_share_abs})

# Package id, name, versions. The version code grows with the version (0.1.0 -> 100).
math(EXPR _xqt_version_code "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}")
set_target_properties(xournal-qt PROPERTIES
    QT_ANDROID_PACKAGE_SOURCE_DIR "${XQT_ANDROID_DIR}"
    QT_ANDROID_PACKAGE_NAME "org.xournalqt.app"
    QT_ANDROID_APP_NAME "Xournal Qt"
    QT_ANDROID_APP_ICON "@mipmap/ic_launcher"
    QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
    QT_ANDROID_VERSION_CODE "${_xqt_version_code}"
    QT_ANDROID_MIN_SDK_VERSION 28      # Qt 6.11's minimum (Android 9)
    QT_ANDROID_TARGET_SDK_VERSION 36   # Android 16
    QT_ANDROID_COMPILE_SDK_VERSION 36
    # QML imports: scan the app's QML only (not the spikes and tests under qt/).
    QT_QML_ROOT_PATH "${CMAKE_CURRENT_LIST_DIR}/../src/app/qml")

