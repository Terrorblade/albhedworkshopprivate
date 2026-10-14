@echo off
rem ---------------------------------------------------------------------------
rem Builds Al Bhed Probe, the fixture recorder, into .\build\AlBhedProbe.dll (32-bit),
rem and installs it.
rem
rem Builds the Al Bhed Workshop library first and links it, so everything the plugin knows
rem about FFX.exe comes from the shared library rather than from its own copy.
rem
rem Plugin sources live under .\plugins\probe\ and are globbed, so adding a .cpp needs no
rem edit here. Include paths are rooted at both the plugin folder and the library's
rem include folder, so an include reads as either "capture/Capture.h" or "ffx/MotionStep.h".
rem
rem The plugin records fixtures for Project Zanarkand. Start a capture with
rem probe.capture=1 in <game dir>\AlBhedWorkshop\albhed_probe.txt or with Scroll Lock.
rem See reversing\PROBE_FIXTURES.md.
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "LOADER=%~dp0"

rem The library first. It brings its own vcvars.
call "%LOADER%build_workshop.bat"
if errorlevel 1 exit /b 1

set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
rem 2>nul as well as >nul: vcvars32 probes for vswhere.exe, which is not on PATH here, and
rem its "not recognized" goes to stderr. The errorlevel check below is what actually
rem catches a broken vcvars, so nothing is lost by hiding it.
call "%VCVARS%" >nul 2>nul
if errorlevel 1 (
    echo ERROR: vcvars32.bat failed. Run it by hand to see why:
    echo        "%VCVARS%"
    exit /b 1
)

set "MODDIR=%LOADER%plugins\probe"
set "LIBINC=%LOADER%workshop\include"
set "OUTDIR=%LOADER%build"
set "OBJDIR=%OUTDIR%\probe"

if exist "%OBJDIR%" rmdir /s /q "%OBJDIR%"
mkdir "%OBJDIR%"

set "SOURCES="
for /r "%MODDIR%" %%f in (*.cpp) do set "SOURCES=!SOURCES! "%%f""
if not defined SOURCES (
    echo ERROR: no .cpp files found under %MODDIR%
    exit /b 1
)

echo === building AlBhedProbe.dll ===
cl /nologo /c /EHsc /MT /O2 /W4 /DNDEBUG /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS ^
   /I"%MODDIR%" /I"%LIBINC%" /Fo"%OBJDIR%\\" !SOURCES!
if errorlevel 1 goto :fail

rem /MAP SO A CRASH REPORT CAN BE READ. There are no PDBs here on purpose, so a
rem report gives module+RVA and nothing else. The map file turns an RVA back into
rem a function name: python tools\mapsym.py build\AlBhedProbe.map 0x5BA13
link /nologo /DLL /MACHINE:X86 /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT /MAP ^
     /OUT:"%OUTDIR%\AlBhedProbe.dll" "%OBJDIR%\*.obj" "%OUTDIR%\AlBhedWorkshop.lib" ^
     kernel32.lib user32.lib gdi32.lib
if errorlevel 1 goto :fail

echo.
echo Built: %OUTDIR%\AlBhedProbe.dll
echo.
set "GAMEDIR=G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster"
set "DLLNAME=AlBhedProbe.dll"

rem INSTALLED, NOT JUST SUGGESTED. Echoing the copy command and stopping reads the same
rem as having installed it, so a test run could measure a stale DLL and pass. If the game
rem is running it has the file open and the copy fails, which is worth one loud line
rem rather than a silent build.
set "INSTALLDIR=%GAMEDIR%\AlBhedWorkshop\plugins"
if not exist "%INSTALLDIR%" mkdir "%INSTALLDIR%"
copy /y "%OUTDIR%\%DLLNAME%" "%INSTALLDIR%\" >nul
if errorlevel 1 (
    echo.
    echo *** NOT INSTALLED ***  %DLLNAME% could not be copied to:
    echo     "%INSTALLDIR%"
    echo     The game is probably running and holding it open. Close it and
    echo     run this again, or the next test measures the old build.
    exit /b 1
)

rem VERIFIED, NOT ASSUMED. copy can report success and still leave an older file
rem behind, for example when a sync tool or an antivirus holds the destination. Compare
rem the two files byte for byte so a stale DLL is caught here and not in a test run.
fc /b "%OUTDIR%\%DLLNAME%" "%INSTALLDIR%\%DLLNAME%" >nul
if errorlevel 1 (
    echo.
    echo *** INSTALLED FILE DIFFERS ***  "%INSTALLDIR%\%DLLNAME%" is not the DLL just built.
    exit /b 1
)

rem QUOTED, because this game's folder name contains an ampersand and cmd splits
rem an unquoted echo on it, which printed a path that stopped at "FFX".
echo Installed and verified: "%INSTALLDIR%\%DLLNAME%"
exit /b 0

:fail
echo BUILD FAILED
exit /b 1
