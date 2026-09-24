@echo off
rem Xournal Qt with a log of what the pen, the fingers and the mouse send (qt/docs/windows.md, "Pen input").
rem Double-click it, draw a few strokes with the pen (light and hard), touch the page, then close Xournal Qt.
rem The log is input-log.txt next to this file; it opens when the program has closed.
setlocal
set "XQT_LOG_INPUT=1"
rem A window of its own, even when Xournal Qt is running already
set "XQT_NO_SINGLE_INSTANCE=1"
rem Qt's messages into the log too, with its own view of the pen (tablet) and the input devices
set "QT_FORCE_STDERR_LOGGING=1"
set "QT_LOGGING_RULES=qt.qpa.input.tablet.debug=true;qt.qpa.input.devices.debug=true"
set "LOG=%~dp0input-log.txt"
echo Xournal Qt input log, %DATE% %TIME% > "%LOG%"
ver >> "%LOG%"
echo Xournal Qt is starting. This window closes with the program; the log goes to %LOG%
"%~dp0bin\xournal-qt.exe" %* >> "%LOG%" 2>&1
start "" notepad "%LOG%"
