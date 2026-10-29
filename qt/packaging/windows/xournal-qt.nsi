; The Windows installer of xournal-qt (qt/docs/development/windows.md, "The installer"), adapted from upstream's
; windows-setup/xournalpp.nsi. qt/scripts/windows-installer.sh builds it from the program folder of windows-deploy.sh:
;
;   makensis -DVERSION=x.y.z -DSOURCE_DIR=<program folder> -DOUTFILE=<setup.exe> -DREPO_DIR=<checkout> xournal-qt.nsi
;
; - For the user who runs it, without admin rights: into %LOCALAPPDATA%\Programs\xournal-qt, every registry entry
;   under HKCU. Every name is xournal-qt's own, so it installs next to Xournal++ (whose installer uses "Xournal++").
; - An update installs into the folder of the installed version, after removing that version's program files (DLLs
;   and Qt plugins differ between versions; a stale one in bin\ can break the start). The old uninstaller is not run:
;   it would delete the file types, and Windows would then forget the user's choice of default PDF app. The settings
;   (%LOCALAPPDATA%\xournal-qt), caches and libraries are never touched, neither by an update nor by the uninstaller.
; - An older version over a newer one asks first; silently (/S) it is refused (exit code 3). A running Xournal Qt is
;   asked to be closed; silently the installer waits 10 s for it, then gives up (exit code 4).
; - "Open with" for .xopp, .xoj and .pdf always; .xopp and .xoj opened by Xournal Qt (a component, on by default,
;   remembered for updates). The default PDF app can only be chosen by the user (Windows protects that choice): the
;   component registers the app for it and opens its page in Settings → Default apps.

Unicode true
ManifestDPIAware true
RequestExecutionLevel user
SetCompressor /SOLID lzma

!include MUI2.nsh
!include x64.nsh
!include LogicLib.nsh
!include FileFunc.nsh
!include WordFunc.nsh
!include Sections.nsh

!ifndef VERSION | SOURCE_DIR | OUTFILE | REPO_DIR
    !error "makensis -DVERSION=x.y.z -DSOURCE_DIR=<program folder> -DOUTFILE=<setup.exe> -DREPO_DIR=<checkout>"
!endif

!define APP_NAME "Xournal Qt"
!define APP_ID "xournal-qt"
!define APP_EXE "xournal-qt.exe"
!define CLI_EXE "xournal-qt-cli.exe"
!define APP_KEY "Software\xournal-qt"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\xournal-qt"
!define APP_PATHS_KEY "Software\Microsoft\Windows\CurrentVersion\App Paths\xournal-qt.exe"
!define APPLICATION_KEY "Software\Classes\Applications\xournal-qt.exe"
!define PROGID_XOPP "xournal-qt.xopp"
!define PROGID_XOJ "xournal-qt.xoj"
!define PROGID_PDF "xournal-qt.pdf"
!define SHORTCUT "Xournal Qt.lnk"
!define URL "https://github.com/R-Fehler/xournal-qt"
!define ICONS "${REPO_DIR}\qt\packaging\windows"

Name "${APP_NAME} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP_ID}"
BrandingText "${APP_NAME} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey ProductName "${APP_NAME}"
VIAddVersionKey ProductVersion "${VERSION}"
VIAddVersionKey FileVersion "${VERSION}"
VIAddVersionKey FileDescription "${APP_NAME} ${VERSION} Setup"
VIAddVersionKey CompanyName "xournal-qt"
VIAddVersionKey LegalCopyright "GPL-2.0-or-later"

Var InstalledVersion  ; the version found in the registry, "" when none is installed
Var IsUpdate          ; 1 when updating an installed version
Var WelcomeText

;--------------------------------
; Pages

!define MUI_ICON "${ICONS}\xournal-qt.ico"
!define MUI_UNICON "${ICONS}\xournal-qt.ico"
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TEXT "$WelcomeText"
!insertmacro MUI_PAGE_WELCOME
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipOnUpdate
!insertmacro MUI_PAGE_LICENSE "${REPO_DIR}\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipOnUpdate
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirectoryLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APP_NAME}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

;--------------------------------
; Shared by the installer and the uninstaller

; Every top-level entry the program folder has ever had: an update removes them before installing the new ones, the
; uninstaller removes them. windows-installer.sh refuses a program folder with an entry missing here.
!macro RemoveProgramFiles
    RMDir /r "$INSTDIR\bin"
    RMDir /r "$INSTDIR\share"
    RMDir /r "$INSTDIR\lib"
    RMDir /r "$INSTDIR\etc"
    Delete "$INSTDIR\README.txt"
    Delete "$INSTDIR\xournal-qt-debug.bat"
    Delete "$INSTDIR\input-log.txt"
!macroend

; Windows keeps a running program's file open for reading only: opening it for writing fails while it runs.
!macro DefineCheckNotRunning PREFIX
Function ${PREFIX}IsRunning  ; $R2: the running program's name, or ""
    StrCpy $R2 ""
    ${If} ${FileExists} "$INSTDIR\bin\${APP_EXE}"
        ClearErrors
        FileOpen $R0 "$INSTDIR\bin\${APP_EXE}" a
        ${If} ${Errors}
            StrCpy $R2 "${APP_NAME}"
            Return
        ${EndIf}
        FileClose $R0
    ${EndIf}
    ${If} ${FileExists} "$INSTDIR\bin\${CLI_EXE}"
        ClearErrors
        FileOpen $R0 "$INSTDIR\bin\${CLI_EXE}" a
        ${If} ${Errors}
            StrCpy $R2 "${CLI_EXE}"
            Return
        ${EndIf}
        FileClose $R0
    ${EndIf}
FunctionEnd

Function ${PREFIX}CheckNotRunning
    StrCpy $R1 0
    check:
    Call ${PREFIX}IsRunning
    ${If} $R2 == ""
        Return
    ${EndIf}
    ${If} ${Silent}
        IntOp $R1 $R1 + 1
        ${If} $R1 <= 10
            Sleep 1000
            Goto check
        ${EndIf}
        DetailPrint "$R2 is running in $INSTDIR: not changed"
        SetErrorLevel 4
        Quit
    ${EndIf}
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$R2 is running. Close all its windows, then click Retry." IDRETRY check
    Abort
FunctionEnd
!macroend
!insertmacro DefineCheckNotRunning ""
!insertmacro DefineCheckNotRunning "un."

; The default program of a file type, taken back only while it is ours
!macro UnsetDefault EXT PROGID
    ReadRegStr $0 HKCU "Software\Classes\${EXT}" ""
    ${If} $0 == "${PROGID}"
        DeleteRegValue HKCU "Software\Classes\${EXT}" ""
    ${EndIf}
!macroend

!macro UnOpenWith EXT PROGID
    DeleteRegValue HKCU "Software\Classes\${EXT}\OpenWithProgids" "${PROGID}"
    DeleteRegKey /ifnosubkeys /ifnovalues HKCU "Software\Classes\${EXT}\OpenWithProgids"
    DeleteRegKey /ifnosubkeys /ifnovalues HKCU "Software\Classes\${EXT}"
!macroend

!macro NotifyShell
    ; SHCNE_ASSOCCHANGED: Explorer reads the file types and icons again
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
!macroend

;--------------------------------
; Installer sections

!macro ProgId PROGID DESCRIPTION ICON
    WriteRegStr HKCU "Software\Classes\${PROGID}" "" "${DESCRIPTION}"
    WriteRegStr HKCU "Software\Classes\${PROGID}" "FriendlyTypeName" "${DESCRIPTION}"
    WriteRegStr HKCU "Software\Classes\${PROGID}\DefaultIcon" "" '"$INSTDIR\bin\${APP_EXE}",${ICON}'
    WriteRegStr HKCU "Software\Classes\${PROGID}\shell" "" "open"
    WriteRegStr HKCU "Software\Classes\${PROGID}\shell\open\command" "" '"$INSTDIR\bin\${APP_EXE}" "%1"'
!macroend

!macro OpenWith EXT PROGID
    WriteRegStr HKCU "Software\Classes\${EXT}\OpenWithProgids" "${PROGID}" ""
    WriteRegStr HKCU "${APPLICATION_KEY}\SupportedTypes" "${EXT}" ""
    WriteRegStr HKCU "${APP_KEY}\Capabilities\FileAssociations" "${EXT}" "${PROGID}"
!macroend

Section "-Close Xournal Qt" SecCheck
    Call CheckNotRunning
SectionEnd

Section "${APP_NAME}" SecApp
    SectionIn RO

    ${If} ${FileExists} "$INSTDIR\bin\${APP_EXE}"
        ${If} $InstalledVersion != ""
            DetailPrint "Removing the program files of ${APP_NAME} $InstalledVersion"
        ${Else}
            DetailPrint "Removing the program files found in $INSTDIR"
        ${EndIf}
        !insertmacro RemoveProgramFiles
    ${EndIf}

    SetOutPath "$INSTDIR"
    File /r "${SOURCE_DIR}\*"
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    WriteRegStr HKCU "${APP_KEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "${APP_KEY}" "Version" "${VERSION}"

    ; Start by name (Win+R "xournal-qt"), and the program as Windows' "Open with" lists it
    WriteRegStr HKCU "${APP_PATHS_KEY}" "" "$INSTDIR\bin\${APP_EXE}"
    WriteRegStr HKCU "${APP_PATHS_KEY}" "Path" "$INSTDIR\bin"
    WriteRegStr HKCU "${APPLICATION_KEY}" "FriendlyAppName" "${APP_NAME}"
    WriteRegStr HKCU "${APPLICATION_KEY}\DefaultIcon" "" '"$INSTDIR\bin\${APP_EXE}",0'
    WriteRegStr HKCU "${APPLICATION_KEY}\shell\open\command" "" '"$INSTDIR\bin\${APP_EXE}" "%1"'

    ; The file types (icon 1: the document icon in xournal-qt.exe, qt/packaging/windows/xournal-qt.rc.in)
    !insertmacro ProgId "${PROGID_XOPP}" "Xournal++ document" 1
    !insertmacro ProgId "${PROGID_XOJ}" "Xournal document" 1
    !insertmacro ProgId "${PROGID_PDF}" "PDF document" 0
    !insertmacro OpenWith ".xopp" "${PROGID_XOPP}"
    !insertmacro OpenWith ".xoj" "${PROGID_XOJ}"
    !insertmacro OpenWith ".pdf" "${PROGID_PDF}"

    ; Settings → Default apps lists the program with these types
    WriteRegStr HKCU "${APP_KEY}\Capabilities" "ApplicationName" "${APP_NAME}"
    WriteRegStr HKCU "${APP_KEY}\Capabilities" "ApplicationDescription" "Handwritten notes and PDF annotation"
    WriteRegStr HKCU "${APP_KEY}\Capabilities" "ApplicationIcon" '"$INSTDIR\bin\${APP_EXE}",0'
    WriteRegStr HKCU "Software\RegisteredApplications" "${APP_ID}" "${APP_KEY}\Capabilities"

    SetOutPath "$INSTDIR\bin"  ; (the shortcuts' working folder)
    CreateShortcut "$SMPROGRAMS\${SHORTCUT}" "$INSTDIR\bin\${APP_EXE}" "" "$INSTDIR\bin\${APP_EXE}" 0

    ; Apps → Installed apps
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" '"$INSTDIR\bin\${APP_EXE}",0'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "xournal-qt"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "${URL}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "HelpLink" "${URL}/issues"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" $0
SectionEnd

Section "Open .xopp and .xoj files with Xournal Qt" SecAssoc
    WriteRegStr HKCU "Software\Classes\.xopp" "" "${PROGID_XOPP}"
    WriteRegStr HKCU "Software\Classes\.xoj" "" "${PROGID_XOJ}"
SectionEnd

Section /o "Desktop shortcut" SecDesktop
    SetOutPath "$INSTDIR\bin"
    CreateShortcut "$DESKTOP\${SHORTCUT}" "$INSTDIR\bin\${APP_EXE}" "" "$INSTDIR\bin\${APP_EXE}" 0
SectionEnd

Section /o "Make Xournal Qt the default PDF app" SecDefaultPdf
    ; Windows lets only the user choose the default app (it ignores and resets a choice a program writes): its
    ; Settings page for Xournal Qt (Windows 11; Windows 10 opens Default apps) shows .pdf with the current app.
    ${IfNot} ${Silent}
        MessageBox MB_OK|MB_ICONINFORMATION "Windows lets only you choose the default PDF app.$\r$\n$\r$\nSettings opens on ${APP_NAME}'s page: click .pdf there and choose ${APP_NAME}."
        ExecShell "open" "ms-settings:defaultapps?registeredAppUser=${APP_ID}"
    ${EndIf}
SectionEnd

Section "-Remember the choices" SecFinish
    ${If} ${SectionIsSelected} ${SecAssoc}
        WriteRegDWORD HKCU "${APP_KEY}" "OpenXopp" 1
    ${Else}
        WriteRegDWORD HKCU "${APP_KEY}" "OpenXopp" 0
        !insertmacro UnsetDefault ".xopp" "${PROGID_XOPP}"
        !insertmacro UnsetDefault ".xoj" "${PROGID_XOJ}"
    ${EndIf}
    ${IfNot} ${SectionIsSelected} ${SecDesktop}
        Delete "$DESKTOP\${SHORTCUT}"
    ${EndIf}
    !insertmacro NotifyShell
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} "The program, its Start menu entry and Open with for .xopp, .xoj and .pdf files."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecAssoc} "Double-clicking a .xopp or .xoj file opens it in ${APP_NAME}."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "A shortcut to ${APP_NAME} on the desktop."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDefaultPdf} "Opens Windows' settings at the end, where you choose ${APP_NAME} for PDF files."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------
; Installer functions

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_OK|MB_ICONSTOP "${APP_NAME} needs 64-bit Windows." /SD IDOK
        SetErrorLevel 2
        Quit
    ${EndIf}
    SetRegView 64
    SetShellVarContext current

    StrCpy $IsUpdate 0
    StrCpy $WelcomeText "This installs ${APP_NAME} ${VERSION} for you (no administrator rights needed).$\r$\n$\r$\n$_CLICK"
    ReadRegStr $InstalledVersion HKCU "${APP_KEY}" "Version"
    ReadRegStr $0 HKCU "${APP_KEY}" "InstallDir"
    ${If} $0 == ""
    ${OrIfNot} ${FileExists} "$0\bin\${APP_EXE}"
        StrCpy $InstalledVersion ""
        Return
    ${EndIf}

    ; An update: in place, whatever /D says
    StrCpy $IsUpdate 1
    StrCpy $INSTDIR $0
    ${VersionCompare} "$InstalledVersion" "${VERSION}" $1  ; 1: the installed one is newer
    ${If} $1 == 1
        MessageBox MB_YESNO|MB_ICONEXCLAMATION|MB_DEFBUTTON2 "${APP_NAME} $InstalledVersion is installed, which is newer than ${VERSION}.$\r$\n$\r$\nReplace it with the older ${VERSION}?" /SD IDNO IDYES downgrade
        SetErrorLevel 3
        Quit
        downgrade:
    ${EndIf}
    ${If} $1 == 0
        StrCpy $WelcomeText "${APP_NAME} ${VERSION} is installed in $INSTDIR. This installs it again.$\r$\n$\r$\nYour settings and libraries stay as they are.$\r$\n$\r$\n$_CLICK"
    ${Else}
        StrCpy $WelcomeText "This updates ${APP_NAME} $InstalledVersion in $INSTDIR to ${VERSION}.$\r$\n$\r$\nYour settings and libraries stay as they are.$\r$\n$\r$\n$_CLICK"
    ${EndIf}

    ; The choices of the last install
    ClearErrors
    ReadRegDWORD $1 HKCU "${APP_KEY}" "OpenXopp"
    ${IfNot} ${Errors}
    ${AndIf} $1 == 0
        !insertmacro UnselectSection ${SecAssoc}
    ${EndIf}
    ${If} ${FileExists} "$DESKTOP\${SHORTCUT}"
        !insertmacro SelectSection ${SecDesktop}
    ${EndIf}
FunctionEnd

Function SkipOnUpdate
    ${If} $IsUpdate == 1
        Abort
    ${EndIf}
FunctionEnd

; A folder of its own: "xournal-qt" is added to a folder chosen without it, and a folder that holds other files asks.
Function DirectoryLeave
    ${GetFileName} "$INSTDIR" $0
    ${If} $0 != "${APP_ID}"
        StrCpy $1 "$INSTDIR" "" -1
        ${If} $1 == "\"
            StrCpy $INSTDIR "$INSTDIR${APP_ID}"
        ${Else}
            StrCpy $INSTDIR "$INSTDIR\${APP_ID}"
        ${EndIf}
    ${EndIf}
    ${DirState} "$INSTDIR" $0  ; 1: has files
    ${If} $0 == 1
    ${AndIfNot} ${FileExists} "$INSTDIR\bin\${APP_EXE}"
        MessageBox MB_YESNO|MB_ICONEXCLAMATION|MB_DEFBUTTON2 "The folder $INSTDIR is not empty.$\r$\nInstall ${APP_NAME} into it anyway?" IDYES done
        Abort
        done:
    ${EndIf}
FunctionEnd

;--------------------------------
; Uninstaller

Section "Uninstall"
    SetRegView 64
    SetShellVarContext current
    Call un.CheckNotRunning

    !insertmacro RemoveProgramFiles
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    Delete "$SMPROGRAMS\${SHORTCUT}"
    Delete "$DESKTOP\${SHORTCUT}"

    ; Only what the installer wrote: other programs' "Open with" entries and defaults stay
    !insertmacro UnsetDefault ".xopp" "${PROGID_XOPP}"
    !insertmacro UnsetDefault ".xoj" "${PROGID_XOJ}"
    !insertmacro UnOpenWith ".xopp" "${PROGID_XOPP}"
    !insertmacro UnOpenWith ".xoj" "${PROGID_XOJ}"
    !insertmacro UnOpenWith ".pdf" "${PROGID_PDF}"
    DeleteRegKey HKCU "Software\Classes\${PROGID_XOPP}"
    DeleteRegKey HKCU "Software\Classes\${PROGID_XOJ}"
    DeleteRegKey HKCU "Software\Classes\${PROGID_PDF}"
    DeleteRegKey HKCU "${APPLICATION_KEY}"
    DeleteRegKey HKCU "${APP_PATHS_KEY}"
    DeleteRegValue HKCU "Software\RegisteredApplications" "${APP_ID}"
    DeleteRegKey HKCU "${APP_KEY}\Capabilities"
    DeleteRegValue HKCU "${APP_KEY}" "InstallDir"
    DeleteRegValue HKCU "${APP_KEY}" "Version"
    DeleteRegValue HKCU "${APP_KEY}" "OpenXopp"
    DeleteRegKey /ifnosubkeys /ifnovalues HKCU "${APP_KEY}"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"

    !insertmacro NotifyShell
SectionEnd

Function un.onInit
    SetRegView 64
    SetShellVarContext current
FunctionEnd
