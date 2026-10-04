@echo off
rem ---------------------------------------------------------------------------
rem Builds the cheat plugin into .\build\AlBhedCheats.dll (32-bit).
rem
rem Builds the Al Bhed Workshop library first and links it, so everything the plugin knows
rem about FFX.exe comes from the shared library rather than from its own copy. This plugin
rem is the second one on the hub, which is why it shares not one line of code with
rem Pilgrimage Together.
rem
rem Plugin sources live under .\plugins\cheats\ and are globbed, so adding a .cpp needs no
rem edit here. Include paths are rooted at both the plugin folder and the library's include
rem folder, so an include reads as either "ui/CheatPanel.h" or "ffx/GameLists.h".
rem
rem Install by copying the DLL into <game dir>\AlBhedWorkshop\plugins\.
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

set "MODDIR=%LOADER%plugins\cheats"
set "LIBINC=%LOADER%workshop\include"
set "IMGUI=%LOADER%third_party\imgui"
set "OUTDIR=%LOADER%build"
set "OBJDIR=%OUTDIR%\cheats"

if exist "%OBJDIR%" rmdir /s /q "%OBJDIR%"
mkdir "%OBJDIR%"

set "SOURCES="
for /r "%MODDIR%" %%f in (*.cpp) do set "SOURCES=!SOURCES! "%%f""
if not defined SOURCES (
    echo ERROR: no .cpp files found under %MODDIR%
    exit /b 1
)

echo === building AlBhedCheats.dll ===
cl /nologo /c /EHsc /MT /O2 /W4 /DNDEBUG /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS ^
   /I"%MODDIR%" /I"%LIBINC%" /I"%IMGUI%" /I"%IMGUI%\backends" /Fo"%OBJDIR%\\" !SOURCES!
if errorlevel 1 goto :fail

link /nologo /DLL /MACHINE:X86 /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT ^
     /OUT:"%OUTDIR%\AlBhedCheats.dll" "%OBJDIR%\*.obj" "%OUTDIR%\AlBhedWorkshop.lib" ^
     kernel32.lib user32.lib gdi32.lib delayimp.lib ^
     /DELAYLOAD:d3dcompiler_47.dll
if errorlevel 1 goto :fail

echo.
echo Built: %OUTDIR%\AlBhedCheats.dll
echo.
echo Install with:
echo   copy "%OUTDIR%\AlBhedCheats.dll" "G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster\AlBhedWorkshop\plugins\"
exit /b 0

:fail
echo BUILD FAILED
exit /b 1
