# Installation and the .deb package (CPack, as upstream Xournal++: `cmake --build <dir> --target package`).
# See ../packaging/README.md.

set(XQT_PACKAGING_DIR "${CMAKE_CURRENT_LIST_DIR}/../packaging")

install(TARGETS xournal-qt RUNTIME DESTINATION bin)
# Resources: page templates, palettes, icons (found at <prefix>/share/xournal-qt, see AppContext)
install(FILES "${XQT_BUILD_RESOURCE_DIR}/pagetemplates.ini" DESTINATION share/xournal-qt)
install(DIRECTORY "${XQT_BUILD_RESOURCE_DIR}/palettes" "${XQT_BUILD_RESOURCE_DIR}/icons" DESTINATION share/xournal-qt)

# Desktop integration
install(FILES "${XQT_PACKAGING_DIR}/xournal-qt.desktop" DESTINATION share/applications)
install(FILES "${XQT_PACKAGING_DIR}/xournal-qt.xml" DESTINATION share/mime/packages)
install(FILES "${XQT_PACKAGING_DIR}/xournal-qt.svg" DESTINATION share/icons/hicolor/scalable/apps)
install(FILES "${XOJ_UPSTREAM_DIR}/ui/pixmaps/application-x-xopp.svg"
              "${XOJ_UPSTREAM_DIR}/ui/pixmaps/application-x-xojpp.svg"
        DESTINATION share/icons/hicolor/scalable/mimetypes)
install(FILES "${XQT_PACKAGING_DIR}/xournal-qt-library.desktop" DESTINATION share/kio/servicemenus
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
install(FILES "${XQT_PACKAGING_DIR}/copyright" DESTINATION share/doc/xournal-qt)

# Package
if(NOT CPACK_PACKAGE_CONTACT)
    # The maintainer: whoever builds it (git identity)
    execute_process(COMMAND git config user.name OUTPUT_VARIABLE _name OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND git config user.email OUTPUT_VARIABLE _mail OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_name AND _mail)
        set(CPACK_PACKAGE_CONTACT "${_name} <${_mail}>")
    else()
        set(CPACK_PACKAGE_CONTACT "xournal-qt")
    endif()
endif()
set(CPACK_GENERATOR "DEB" CACHE STRING "CPack generator")
set(CPACK_PACKAGE_NAME "xournal-qt")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Handwritten notes and PDF annotation (Qt version of Xournal++)")
set(CPACK_PACKAGE_DESCRIPTION
    "Xournal Qt is a Qt 6 frontend for the Xournal++ core: note taking with a pen or stylus, PDF annotation,\n tabs, a document library and a touch-friendly interface. It reads and writes Xournal++ files (.xopp).")
set(CPACK_OUTPUT_FILE_PREFIX packages)
set(CPACK_STRIP_FILES ON)
# Directories 755 whatever the umask of the build (lintian)
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ
    WORLD_EXECUTE)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/xournalpp/xournalpp")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Not found by dpkg-shlibdeps: the QML modules and the SVG icon plugin (KDE neon names | Ubuntu names)
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "qt6-declarative | qml6-module-qtquick-controls, qt6-declarative | qml6-module-qtquick-dialogs, qt6-declarative | qml6-module-qtquick-layouts, qt6-svg | qt6-svg-plugins")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland")
include(CPack)
