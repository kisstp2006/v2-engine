@echo off
setlocal enabledelayedexpansion

rem PATH sanitization: GNU find.exe (Git/MinGW) shadows Windows find.exe,
rem which silently breaks MAKE.bat's engine.ffi generation.
set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem;%PATH%"

pushd "%~dp0..\.."

rem Auto-discovery: recursively gather ALL .cpp and .c files under
rem tools\editor-cpp\. New panel / module = new file, BUILD.bat unchanged.
rem .c files compile in C mode (cl /Tc), .cpp in C++ (/Tp) — automatic.
set "SRCS="
for /R tools\editor-cpp %%f in (*.cpp) do set "SRCS=!SRCS! %%f"
for /R tools\editor-cpp %%f in (*.c)   do set "SRCS=!SRCS! %%f"

rem /std:c++17 is required for <filesystem> (MAKE.bat does not set it).
rem /Fe:editor-cpp.exe pins the exe name (otherwise it follows the first file).
rem .\ prefix because the current dir is not in PATH after sanitization.
rem
rem `retail` tier = MAKE.bat's most aggressive build:
rem   compile: /DNDEBUG=3 /Os /Ox /O2 /Oy /GL /GF /Gw /arch:AVX2
rem   link:    /OPT:ICF,3 /LTCG
rem
rem Differences from `release` (the previous setting):
rem   /Gw         — separate COMDAT per global → linker drops unused
rem   /arch:AVX2  — AVX2 SIMD auto-vectorization (256-bit vector math)
rem   /DNDEBUG=3  — one assert-stripping level deeper than release
rem   /LTCG       — explicit link-time codegen (extra cross-module passes)
rem
rem Safe on this user's machine: Intel Core i3-10100F (Comet Lake, 2020)
rem supports AVX2 since Haswell (2013). Most modern x64 CPUs do too. If
rem you ever target a pre-2013 CPU, drop back to `release`.
rem
rem (NEVER `devel` — that's /DNDEBUG=1 with ZERO optimization flags. The
rem motor's macro-heavy paths run 3-5× slower without /O2 /GL /GF.)
call .\MAKE.bat retail /std:c++17 /Fe:editor-cpp.exe !SRCS!
set rc=%errorlevel%

popd

endlocal & exit /b %rc%
