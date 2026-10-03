# Al Bhed Workshop loader - 32-bit proxy DLL.
#
# FFX.exe is a 32-bit process, so this MUST be configured for Win32. CMake will
# not pick that up on its own on a 64-bit machine, you have to ask for it:
#
#   cmake -S . -B build-x86 -G "Visual Studio 18 2026" -A Win32
#   cmake --build build-x86 --config Release
#
# Outputs land in build-x86\Release\:
#   dinput8.dll   primary proxy target
#   version.dll   fallback proxy target
#
# build.bat in this folder does the same thing with a plain cl.exe + link.exe
# pair and no generator to argue with, and is the quicker path.

cmake_minimum_required(VERSION 3.20)
project(pilgrimage_loader LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "This proxy uses MSVC x86 inline assembly (__declspec(naked) + __asm). Build it with MSVC.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "Configured for ${CMAKE_SIZEOF_VOID_P}-byte pointers. FFX.exe is 32-bit; "
        "reconfigure with -A Win32.")
endif()

function(pilgrimage_add_proxy target outname)
    add_library(${target} SHARED workshop_proxy.cpp)

    # The output file name is what the Windows loader looks for, so it has to be
    # exactly the imported DLL name with no prefix or decoration.
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME        ${outname}
        PREFIX             ""
        SUFFIX             ".dll"
        MSVC_RUNTIME_LIBRARY "MultiThreaded")   # static CRT: no VC redist in the game folder

    target_compile_definitions(${target} PRIVATE
        WIN32 _WINDOWS NDEBUG _CRT_SECURE_NO_WARNINGS)

    # /GS- : the forwarding stubs are __declspec(naked), so no prologue exists for
    # a security cookie to live in.
    target_compile_options(${target} PRIVATE /W4 /GS- /EHsc)
    target_link_libraries(${target} PRIVATE kernel32 user32)
    target_link_options(${target} PRIVATE /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF)
endfunction()

pilgrimage_add_proxy(pilgrimage_dinput8 dinput8)

pilgrimage_add_proxy(pilgrimage_version version)
target_compile_definitions(pilgrimage_version PRIVATE FFXCOOP_TARGET_VERSION)
