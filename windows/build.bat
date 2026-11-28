@echo off
setlocal
cd /d "%~dp0"

echo ==> Building Whisku.exe (Windows native C++17 / Win32 + GDI+ + Raw Input)...

where cl >nul 2>nul
if %ERRORLEVEL%==0 (
    rc /nologo /fo app.res app.rc
    if errorlevel 1 exit /b 1
    cl /nologo /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE ^
       /W3 src\main.cpp app.res /Fe:Whisku.exe ^
       /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:app.manifest ^
       user32.lib gdi32.lib gdiplus.lib shell32.lib advapi32.lib winhttp.lib wtsapi32.lib bcrypt.lib ole32.lib oleaut32.lib
    if errorlevel 1 exit /b 1
    del /q main.obj app.res 2>nul
    echo ==> Built successfully with MSVC: %~dp0Whisku.exe
    exit /b 0
)

where g++ >nul 2>nul
if %ERRORLEVEL%==0 (
    windres app.rc -O coff -o app_res.o
    if errorlevel 1 exit /b 1
    g++ -std=c++17 -O2 -mwindows -municode -DUNICODE -D_UNICODE -static ^
        src\main.cpp app_res.o -o Whisku.exe ^
        -luser32 -lgdi32 -lgdiplus -lshell32 -ladvapi32 -lwinhttp -lwtsapi32 -lbcrypt -lole32 -loleaut32 -luuid
    if errorlevel 1 exit /b 1
    del /q app_res.o 2>nul
    echo ==> Built successfully with MinGW: %~dp0Whisku.exe
    exit /b 0
)

echo [ERROR] Neither MSVC (cl.exe) nor MinGW (g++.exe) found in PATH.
echo Please run from "x64 Native Tools Command Prompt for VS" or install Visual Studio Build Tools.
exit /b 1
