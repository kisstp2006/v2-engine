// STL ELŐSZÖR.
#include <string>
#include <vector>

#include <windows.h>

#include "process_launcher.h"

namespace editor {

bool launchEditorWithProject(const std::string& exe,
                             const std::string& projectPath) {
    if (exe.empty()) return false;

    // CreateProcess command-line: az első token hagyományosan az exe név,
    // és az API a teljes string-et átadja a child-nek (CommandLineToArgv
    // bontja). Idézőjelezünk space-es path-ok miatt. Üres projectPath esetén
    // a flag elmarad → picker módban indul.
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

    // CreateProcessA mutable buffer-t vár (lpCommandLine nem const).
    std::vector<char> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back('\0');

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    BOOL ok = ::CreateProcessA(
        exe.c_str(),              // lpApplicationName — teljes exe path
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
