# xournal-qt: vcpkg's built-in arm64-android triplet (static libraries, the shared C++ runtime c++_shared that Qt
# uses, API 28 = Qt 6.11's minimum), built in release only to halve the dependency build time. Static libraries end up
# inside libxournal-qt_arm64-v8a.so, so androiddeployqt has no extra .so files to find.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Android)
set(VCPKG_CMAKE_SYSTEM_VERSION 28)
set(VCPKG_MAKE_BUILD_TRIPLET "--host=aarch64-linux-android")
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DANDROID_ABI=arm64-v8a)
set(VCPKG_BUILD_TYPE release)
