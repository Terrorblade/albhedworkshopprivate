@echo off
rem ---------------------------------------------------------------------------
rem Builds the Al Bhed Workshop library, the shared FFX.exe mod kit, into
rem .\build\AlBhedWorkshop.lib (32-bit static).
rem
rem Every plugin DLL links this instead of carrying its own copy of the
rem addresses, the structure layouts and the hook plumbing. See
rem workshop\include\ffx\Ffx.h for what is in it and the startup contract.
rem
rem Callable from another batch file: it sets up its own vcvars and leaves
rem nothing behind. build_pilgrimage.bat calls it, so you rarely need to run it
rem by hand.
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS%" (
    echo ERROR: vcvars32.bat not found at:
    echo        %VCVARS%
    exit /b 1
)
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1

set "LOADER=%~dp0"
set "LIBDIR=%LOADER%workshop"
set "OUTDIR=%LOADER%build"
set "OBJDIR=%OUTDIR%\workshop"

if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if exist "%OBJDIR%" rmdir /s /q "%OBJDIR%"
mkdir "%OBJDIR%"

rem Basenames must stay unique across the library, because all the .obj files
rem land in one folder.
set "SOURCES="
for /r "%LIBDIR%\src" %%f in (*.cpp) do set "SOURCES=!SOURCES! "%%f""
if not defined SOURCES (
    echo ERROR: no .cpp files found under %LIBDIR%\src
    exit /b 1
)

echo === building AlBhedWorkshop.lib ===
cl /nologo /c /EHsc /MT /O2 /W4 /DNDEBUG /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS ^
   /I"%LIBDIR%\include" /Fo"%OBJDIR%\\" !SOURCES!
if errorlevel 1 goto :fail

lib /nologo /MACHINE:X86 /OUT:"%OUTDIR%\AlBhedWorkshop.lib" "%OBJDIR%\*.obj"
if errorlevel 1 goto :fail

echo Built: %OUTDIR%\AlBhedWorkshop.lib
exit /b 0

:fail
echo AL BHED WORKSHOP BUILD FAILED
exit /b 1
