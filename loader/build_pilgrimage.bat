@echo off
rem ---------------------------------------------------------------------------
rem Builds Pilgrimage Together, the co-op mod, into .\build\PilgrimageTogether.dll (32-bit).
rem
rem Builds the Al Bhed Workshop library first and links it, so everything the plugin knows about
rem FFX.exe comes from the shared library rather than from its own copy.
rem
rem Plugin sources live under .\plugins\pilgrimage\ and are globbed, so adding a
rem .cpp needs no edit here. Include paths are rooted at both the plugin folder
rem and the library's include folder, so an include reads as either
rem "clones/CloneRoster.h" or "ffx/Character.h".
rem
rem Install by copying the DLL into <game dir>\AlBhedWorkshop\plugins\.
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "LOADER=%~dp0"

rem The library first. It brings its own vcvars.
call "%LOADER%build_workshop.bat"
if errorlevel 1 exit /b 1

set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1

set "MODDIR=%LOADER%plugins\pilgrimage"
set "LIBINC=%LOADER%workshop\include"
set "OUTDIR=%LOADER%build"
set "OBJDIR=%OUTDIR%\pilgrimage"

if exist "%OBJDIR%" rmdir /s /q "%OBJDIR%"
mkdir "%OBJDIR%"

set "SOURCES="
for /r "%MODDIR%" %%f in (*.cpp) do set "SOURCES=!SOURCES! "%%f""
if not defined SOURCES (
    echo ERROR: no .cpp files found under %MODDIR%
    exit /b 1
)

echo === building PilgrimageTogether.dll ===
cl /nologo /c /EHsc /MT /O2 /W4 /DNDEBUG /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS ^
   /I"%MODDIR%" /I"%LIBINC%" /Fo"%OBJDIR%\\" !SOURCES!
if errorlevel 1 goto :fail

link /nologo /DLL /MACHINE:X86 /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT ^
     /OUT:"%OUTDIR%\PilgrimageTogether.dll" "%OBJDIR%\*.obj" "%OUTDIR%\AlBhedWorkshop.lib" ^
     kernel32.lib user32.lib gdi32.lib
if errorlevel 1 goto :fail

echo.
echo Built: %OUTDIR%\PilgrimageTogether.dll
echo.
echo Install with:
echo   copy "%OUTDIR%\PilgrimageTogether.dll" "G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster\AlBhedWorkshop\plugins\"
exit /b 0

:fail
echo BUILD FAILED
exit /b 1
