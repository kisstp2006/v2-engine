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
call .\MAKE.bat devel /std:c++17 /Fe:editor-cpp.exe !SRCS!
set rc=%errorlevel%

popd

endlocal & exit /b %rc%
