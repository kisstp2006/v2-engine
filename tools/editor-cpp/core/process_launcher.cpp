// STL FIRST.
#include <string>
#include <vector>

#include <windows.h>

#include "process_launcher.h"

namespace editor {

bool launchEditorWithProject(const std::string& exe,
                             const std::string& projectPath) {
    if (exe.empty()) return false;

    // CreateProcess command-line: the first token is traditionally the exe
    // name, and the API passes the whole string to the child (CommandLineToArgv
    // splits it). We quote because of space-containing paths. With empty
    // projectPath the flag is omitted → starts in picker mode.
    std::string cmdLine;
    cmdLine.reserve(exe.size() + projectPath.size() + 32);
    cmdLine += '"';
    cmdLine += exe;
    cmdLine += '"';
    if (!projectPath.empty()) {
        cmdLine += " --project \"";
        cmdLine += projectPath;
        cmdLine += '"';
    }

    // CreateProcessA expects a mutable buffer (lpCommandLine is not const).
    std::vector<char> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back('\0');

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    BOOL ok = ::CreateProcessA(
        exe.c_str(),              // lpApplicationName — full exe path
        buf.data(),               // lpCommandLine — mutable
        nullptr, nullptr,
        FALSE, 0, nullptr, nullptr,
        &si, &pi);
    if (!ok) return false;

    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    return true;
}

}  // namespace editor
