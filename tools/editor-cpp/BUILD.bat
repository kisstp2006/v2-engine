@echo off
setlocal enabledelayedexpansion

rem PATH sanitization: GNU find.exe (Git/MinGW) elfedi a Windows find.exe-t,
rem ami csendben megtöri a MAKE.bat engine.ffi generálását.
set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem;%PATH%"

pushd "%~dp0..\.."

rem Auto-discovery: a tools\editor-cpp\ alatti ÖSSZES .cpp és .c fájlt
rem rekurzív összegyűjtjük. Új panel / modul = új fájl, BUILD.bat változatlan.
rem A .c fájlok C-módban fordulnak (cl /Tc), a .cpp-k C++ (/Tp) — automatikus.
set "SRCS="
for /R tools\editor-cpp %%f in (*.cpp) do set "SRCS=!SRCS! %%f"
for /R tools\editor-cpp %%f in (*.c)   do set "SRCS=!SRCS! %%f"

rem /std:c++17 kell a <filesystem>-hez (MAKE.bat alapban nem teszi be).
rem /Fe:editor-cpp.exe fix exe-név (egyébként az első fájl alapján).
rem .\ prefix mert a PATH sanitization után a current dir nincs a PATH-ban.
call .\MAKE.bat devel /std:c++17 /Fe:editor-cpp.exe !SRCS!
set rc=%errorlevel%

popd

endlocal & exit /b %rc%
