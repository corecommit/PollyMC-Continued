!include "MUI2.nsh"

; BUILD_DIR can be passed on the command line: makensis /DBUILD_DIR=C:\build pollymc_installer.nsi
!ifndef BUILD_DIR
    !define BUILD_DIR "C:\pollymc_build"
!endif

Name "PollyMC-Continued"
OutFile "PollyMC-Continued-9.0.0-Setup.exe"
InstallDir "$PROGRAMFILES64\PollyMC-Continued"
RequestExecutionLevel admin

Function .onInit
    ; Check if old installation exists
    ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PollyMC-Continued" "UninstallString"
    StrCmp $0 "" fresh_install

    ; Old installation found - ask to upgrade
    MessageBox MB_YESNO "PollyMC-Continued is already installed. Do you want to upgrade?$\n$\nYour settings and instances will be kept." IDYES upgrade IDNO cancel

    cancel:
        Abort

    upgrade:
        ; Close running instances
        nsExec::ExecToStack 'taskkill /F /IM pollymc.exe'
        Pop $0
        Sleep 1000
        ; Clean old files (but keep user data)
        RMDir /r "$INSTDIR"
        Goto fresh_install

    fresh_install:
FunctionEnd

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\pollymc.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch PollyMC-Continued"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Install"
    SetOutPath "$INSTDIR"
    File /r "${BUILD_DIR}\*.*"

    CreateDirectory "$SMPROGRAMS\PollyMC-Continued"
    CreateShortcut "$SMPROGRAMS\PollyMC-Continued\PollyMC-Continued.lnk" "$INSTDIR\pollymc.exe"
    CreateShortcut "$SMPROGRAMS\PollyMC-Continued\Uninstall.lnk" "$INSTDIR\uninstall.exe"
    CreateShortcut "$DESKTOP\PollyMC-Continued.lnk" "$INSTDIR\pollymc.exe"

    WriteUninstaller "$INSTDIR\uninstall.exe"

    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PollyMC-Continued" "DisplayName" "PollyMC-Continued"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PollyMC-Continued" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PollyMC-Continued" "DisplayVersion" "9.0.0"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PollyMC-Continued" "Publisher" "PollyMC-Continued Contributors"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PollyMC-Continued" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PollyMC-Continued" "NoRepair" 1
SectionEnd

Section "Uninstall"
    RMDir /r "$INSTDIR"
    Delete "$DESKTOP\PollyMC-Continued.lnk"
    RMDir /r "$SMPROGRAMS\PollyMC-Continued"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PollyMC-Continued"
SectionEnd
