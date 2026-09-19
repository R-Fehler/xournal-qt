# M0 spike: compare Qt Quick vs QWidget canvas hosts for tablet/touch input (see qt/docs/adr/0001-ui-host.md).
qt_add_executable(xqt-inkpad
    main.cpp
    Diagnostics.h Diagnostics.cpp
    InkModel.h InkModel.cpp
    Viewport.h Viewport.cpp
    InputRouter.h InputRouter.cpp
    SpikeContext.h SpikeContext.cpp
    WidgetHost.h WidgetHost.cpp
)

qt_add_qml_module(xqt-inkpad
    URI Inkpad
    VERSION 1.0
    QML_FILES Main.qml
    SOURCES QuickCanvas.h QuickCanvas.cpp
)

target_link_libraries(xqt-inkpad PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::QuickControls2
    PkgConfig::XQT_CAIRO PkgConfig::XQT_GLIB
)
