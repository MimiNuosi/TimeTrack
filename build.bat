@echo off
call "D:\Program Files\VS2022\VS2022\VC\Auxiliary\Build\vcvars64.bat" > nul
if errorlevel 1 (
    echo vcvars64.bat failed
    exit /b 1
)
cl.exe /std:c++17 /EHsc /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /Fe:TimeTrack.exe /nologo main.cpp JsonHelper.cpp DataStore.cpp ConfigManager.cpp IgnoreManager.cpp WindowMonitor.cpp IdleDetector.cpp AppNameResolver.cpp TimerEngine.cpp TrayManager.cpp SingleInstance.cpp PanelUI.cpp IgnoreListDialog.cpp SettingsDialog.cpp app.res /link user32.lib shell32.lib comctl32.lib wtsapi32.lib advapi32.lib gdi32.lib
