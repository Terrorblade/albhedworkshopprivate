@echo off
rem ---------------------------------------------------------------------------
rem Builds the example plugin into .\build\example_plugin.dll (32-bit).
rem Install by copying it into <game dir>\AlBhedWorkshop\plugins\.
rem ---------------------------------------------------------------------------
setlocal

set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS%" (
    echo ERROR: vcvars32.bat not found at:
    echo        %VCVARS%
    exit /b 1
)
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1

set "SRCDIR=%~dp0"
set "OUTDIR=%SRCDIR%build"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
pushd "%OUTDIR%"

echo === building example_plugin.dll ===
cl /nologo /c /EHsc /MT /O2 /W4 /DNDEBUG /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS ^
   /Fo"example_plugin.obj" "%SRCDIR%plugins_example\example_plugin.cpp"
if errorlevel 1 goto :fail
link /nologo /DLL /MACHINE:X86 /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT ^
     /OUT:"example_plugin.dll" "example_plugin.obj" kernel32.lib
if errorlevel 1 goto :fail

popd
echo.
echo Built: %OUTDIR%\example_plugin.dll
exit /b 0

:fail
popd
echo BUILD FAILED
exit /b 1
