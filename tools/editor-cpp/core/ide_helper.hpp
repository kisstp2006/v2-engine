// ide_helper.hpp
// Single-header IDE detection and launcher utility (C++17, Windows-focused, with POSIX fallback).
//
// Usage:
//     #include "ide_helper.hpp"
//     auto ides = ide_helper::detect_all();
//     for (const auto& ide : ides) {
//         std::cout << ide.name << " @ " << ide.executable << "\n";
//     }
//     ide_helper::open_in_ide(ides.front(), "C:/projects/MyGame");
//
// Detection coverage:
//     - Visual Studio (via vswhere.exe, 2017+)
//     - Visual Studio Code (user + system install + PATH)
//     - JetBrains CLion / Rider / RustRover (Toolbox + standalone)
//     - Qt Creator (default install path)
//     - Generic fallback via Uninstall registry scan
//
// Notes:
//     - Header-only. No dependencies beyond C++17 <filesystem> and Win32.
//     - Designed for an ImGui-based project launcher: cheap to call once at startup,
//       cache the result, refresh on demand.
//     - Thread-safe to call concurrently (no shared mutable state).

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <array>
#include <algorithm>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <shellapi.h>
#endif

namespace ide_helper {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
//  Types
// ---------------------------------------------------------------------------

enum class IDE {
    VSCode,
    VSCodeInsiders,
    VisualStudio,
    CLion,
    Rider,
    RustRover,
    IntelliJ,
    QtCreator,
    Unknown,
};

struct IDEInfo {
    IDE         id      = IDE::Unknown;
    std::string name;            // human-readable
    std::string version;         // empty if unknown
    fs::path    executable;      // full path to launcher binary
    fs::path    install_root;    // install dir (may equal executable.parent_path())
};

// ---------------------------------------------------------------------------
//  Internal helpers
// ---------------------------------------------------------------------------

namespace detail {

inline std::string env(const char* name) {
#ifdef _WIN32
    char* buf = nullptr;
    size_t sz = 0;
    if (_dupenv_s(&buf, &sz, name) != 0 || !buf) return {};
    std::string out(buf);
    free(buf);
    return out;
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
#endif
}

inline std::optional<fs::path> first_existing(const std::vector<fs::path>& candidates) {
    std::error_code ec;
    for (const auto& p : candidates) {
        if (!p.empty() && fs::exists(p, ec)) return p;
    }
    return std::nullopt;
}

inline std::optional<fs::path> find_in_path(const std::string& exe_name) {
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    DWORD len = SearchPathA(nullptr, exe_name.c_str(), nullptr, MAX_PATH, buf, nullptr);
    if (len > 0 && len < MAX_PATH) return fs::path(buf);
    // try with .exe extension explicitly
    std::string with_ext = exe_name + ".exe";
    len = SearchPathA(nullptr, with_ext.c_str(), nullptr, MAX_PATH, buf, nullptr);
    if (len > 0 && len < MAX_PATH) return fs::path(buf);
    return std::nullopt;
#else
    std::string cmd = "command -v " + exe_name + " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return std::nullopt;
    char buf[1024] = {};
    std::string out;
    while (fgets(buf, sizeof(buf), pipe)) out += buf;
    pclose(pipe);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    if (out.empty()) return std::nullopt;
    return fs::path(out);
#endif
}

// Run a command and capture stdout (line-by-line).
inline std::vector<std::string> run_capture(const std::string& cmd) {
    std::vector<std::string> lines;
#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) return lines;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if (!line.empty()) lines.push_back(std::move(line));
    }
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return lines;
}

#ifdef _WIN32
// Read a string value from the registry. Returns empty on failure.
inline std::string reg_read_string(HKEY root, const std::string& subkey, const std::string& value) {
    HKEY h;
    if (RegOpenKeyExA(root, subkey.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &h) != ERROR_SUCCESS) {
        // try 32-bit view
        if (RegOpenKeyExA(root, subkey.c_str(), 0, KEY_READ | KEY_WOW64_32KEY, &h) != ERROR_SUCCESS) {
            return {};
        }
    }
    DWORD type = 0, size = 0;
    if (RegQueryValueExA(h, value.c_str(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS
        || (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0) {
        RegCloseKey(h);
        return {};
    }
    std::string out(size, '\0');
    if (RegQueryValueExA(h, value.c_str(), nullptr, &type,
                         reinterpret_cast<LPBYTE>(out.data()), &size) != ERROR_SUCCESS) {
        RegCloseKey(h);
        return {};
    }
    RegCloseKey(h);
    // strip embedded null terminator
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

// Enumerate Uninstall keys looking for an entry whose DisplayName contains `needle`.
// Returns (DisplayName, InstallLocation, DisplayVersion) tuples.
struct UninstallEntry {
    std::string display_name;
    std::string install_location;
    std::string display_version;
};

inline std::vector<UninstallEntry> enum_uninstall(const std::string& needle_lower) {
    std::vector<UninstallEntry> results;
    auto scan = [&](HKEY root, REGSAM view) {
        HKEY h;
        if (RegOpenKeyExA(root,
                          "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
                          0, KEY_READ | view, &h) != ERROR_SUCCESS) return;
        char name[512];
        DWORD i = 0, name_size = sizeof(name);
        while (RegEnumKeyExA(h, i++, name, &name_size, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            std::string subkey = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\";
            subkey += name;
            auto display = reg_read_string(root, subkey, "DisplayName");
            if (!display.empty()) {
                std::string lower = display;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (lower.find(needle_lower) != std::string::npos) {
                    UninstallEntry e;
                    e.display_name      = display;
                    e.install_location  = reg_read_string(root, subkey, "InstallLocation");
                    e.display_version   = reg_read_string(root, subkey, "DisplayVersion");
                    results.push_back(std::move(e));
                }
            }
            name_size = sizeof(name);
        }
        RegCloseKey(h);
    };
    scan(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY);
    scan(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY);
    scan(HKEY_CURRENT_USER,  KEY_WOW64_64KEY);
    return results;
}
#endif // _WIN32

} // namespace detail

// ---------------------------------------------------------------------------
//  Per-IDE detectors
// ---------------------------------------------------------------------------

inline std::optional<IDEInfo> detect_vscode(bool insiders = false) {
    const std::string folder = insiders ? "Microsoft VS Code Insiders" : "Microsoft VS Code";
    const std::string exe    = insiders ? "Code - Insiders.exe"        : "Code.exe";
    const std::string pathExe= insiders ? "code-insiders"              : "code";

    std::vector<fs::path> candidates;
#ifdef _WIN32
    auto local = detail::env("LOCALAPPDATA");
    auto pf    = detail::env("ProgramFiles");
    auto pf86  = detail::env("ProgramFiles(x86)");
    if (!local.empty()) candidates.push_back(fs::path(local) / "Programs" / folder / exe);
    if (!pf.empty())    candidates.push_back(fs::path(pf)    / folder / exe);
    if (!pf86.empty())  candidates.push_back(fs::path(pf86)  / folder / exe);
#else
    candidates.push_back(fs::path("/usr/bin") / pathExe);
    candidates.push_back(fs::path("/usr/local/bin") / pathExe);
    candidates.push_back(fs::path("/snap/bin") / pathExe);
    candidates.push_back(fs::path("/Applications") /
                         (insiders ? "Visual Studio Code - Insiders.app" : "Visual Studio Code.app") /
                         "Contents" / "MacOS" / "Electron");
#endif

    auto found = detail::first_existing(candidates);
    if (!found) found = detail::find_in_path(pathExe);
    if (!found) return std::nullopt;

    IDEInfo info;
    info.id           = insiders ? IDE::VSCodeInsiders : IDE::VSCode;
    info.name         = insiders ? "Visual Studio Code Insiders" : "Visual Studio Code";
    info.executable   = *found;
    info.install_root = found->parent_path();
    return info;
}

inline std::vector<IDEInfo> detect_visual_studio() {
    std::vector<IDEInfo> results;
#ifdef _WIN32
    auto pf86 = detail::env("ProgramFiles(x86)");
    if (pf86.empty()) return results;

    fs::path vswhere = fs::path(pf86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
    if (!fs::exists(vswhere)) return results;

    // -all: include incomplete installs.  -prerelease: include previews.
    // -format value -property X gives one value per line, in install order.
    auto paths    = detail::run_capture("\"" + vswhere.string() +
                                        "\" -all -prerelease -format value -property installationPath");
    auto versions = detail::run_capture("\"" + vswhere.string() +
                                        "\" -all -prerelease -format value -property installationVersion");
    auto names    = detail::run_capture("\"" + vswhere.string() +
                                        "\" -all -prerelease -format value -property displayName");

    for (size_t i = 0; i < paths.size(); ++i) {
        fs::path devenv = fs::path(paths[i]) / "Common7" / "IDE" / "devenv.exe";
        if (!fs::exists(devenv)) continue;
        IDEInfo info;
        info.id           = IDE::VisualStudio;
        info.name         = (i < names.size() && !names[i].empty()) ? names[i] : "Visual Studio";
        info.version      = (i < versions.size()) ? versions[i] : "";
        info.executable   = devenv;
        info.install_root = paths[i];
        results.push_back(std::move(info));
    }
#endif
    return results;
}

// Find a JetBrains IDE by its Toolbox binary prefix (e.g. "clion", "rider", "rustrover", "idea").
inline std::optional<IDEInfo> detect_jetbrains(IDE id,
                                                const std::string& bin_prefix,
                                                const std::string& display_name) {
#ifdef _WIN32
    std::vector<fs::path> candidate_exes = {
        bin_prefix + "64.exe",
        bin_prefix + ".exe",
    };

    // 1. Toolbox install
    auto local = detail::env("LOCALAPPDATA");
    if (!local.empty()) {
        fs::path toolbox_apps = fs::path(local) / "Programs";
        std::error_code ec;
        if (fs::exists(toolbox_apps, ec)) {
            // Toolbox v2 layout: %LOCALAPPDATA%\Programs\<IDE-Name>\bin\<exe>
            for (auto& entry : fs::directory_iterator(toolbox_apps, ec)) {
                if (!entry.is_directory()) continue;
                fs::path bin = entry.path() / "bin";
                if (!fs::exists(bin, ec)) continue;
                for (const auto& cand : candidate_exes) {
                    fs::path exe = bin / cand;
                    if (fs::exists(exe, ec)) {
                        IDEInfo info;
                        info.id           = id;
                        info.name         = display_name;
                        info.executable   = exe;
                        info.install_root = entry.path();
                        return info;
                    }
                }
            }
        }
        // Legacy Toolbox layout: %LOCALAPPDATA%\JetBrains\Toolbox\apps\<IDE>\ch-0\<version>\bin\<exe>
        fs::path legacy = fs::path(local) / "JetBrains" / "Toolbox" / "apps";
        if (fs::exists(legacy, ec)) {
            for (auto& entry : fs::recursive_directory_iterator(legacy, ec)) {
                if (!entry.is_regular_file()) continue;
                auto fname = entry.path().filename().string();
                if (std::find(candidate_exes.begin(), candidate_exes.end(), fname) != candidate_exes.end()) {
                    IDEInfo info;
                    info.id           = id;
                    info.name         = display_name;
                    info.executable   = entry.path();
                    info.install_root = entry.path().parent_path().parent_path();
                    return info;
                }
            }
        }
    }

    // 2. Standalone install — scan Uninstall registry for "<DisplayName>"
    auto entries = detail::enum_uninstall(display_name.empty() ? bin_prefix :
                                          std::string(1, std::tolower(static_cast<unsigned char>(display_name[0]))) +
                                          display_name.substr(1));
    // simpler: search by display_name lowercased
    {
        std::string needle = display_name;
        std::transform(needle.begin(), needle.end(), needle.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        entries = detail::enum_uninstall(needle);
    }
    for (const auto& e : entries) {
        if (e.install_location.empty()) continue;
        for (const auto& cand : candidate_exes) {
            fs::path exe = fs::path(e.install_location) / "bin" / cand;
            std::error_code ec;
            if (fs::exists(exe, ec)) {
                IDEInfo info;
                info.id           = id;
                info.name         = display_name;
                info.version      = e.display_version;
                info.executable   = exe;
                info.install_root = e.install_location;
                return info;
            }
        }
    }
#else
    (void)id; (void)bin_prefix; (void)display_name;
#endif
    return std::nullopt;
}

inline std::optional<IDEInfo> detect_clion()     { return detect_jetbrains(IDE::CLion,     "clion",     "CLion"); }
inline std::optional<IDEInfo> detect_rider()     { return detect_jetbrains(IDE::Rider,     "rider",     "Rider"); }
inline std::optional<IDEInfo> detect_rustrover() { return detect_jetbrains(IDE::RustRover, "rustrover", "RustRover"); }
inline std::optional<IDEInfo> detect_intellij()  { return detect_jetbrains(IDE::IntelliJ,  "idea",      "IntelliJ IDEA"); }

inline std::optional<IDEInfo> detect_qt_creator() {
#ifdef _WIN32
    std::vector<fs::path> candidates;
    auto pf   = detail::env("ProgramFiles");
    auto pf86 = detail::env("ProgramFiles(x86)");
    for (const auto& base : { pf, pf86 }) {
        if (base.empty()) continue;
        // Common: C:\Qt\Tools\QtCreator\bin\qtcreator.exe
        candidates.push_back(fs::path("C:/Qt/Tools/QtCreator/bin/qtcreator.exe"));
        candidates.push_back(fs::path(base) / "Qt Creator" / "bin" / "qtcreator.exe");
    }
    auto found = detail::first_existing(candidates);
    if (!found) found = detail::find_in_path("qtcreator");
    if (!found) return std::nullopt;
    IDEInfo info;
    info.id           = IDE::QtCreator;
    info.name         = "Qt Creator";
    info.executable   = *found;
    info.install_root = found->parent_path().parent_path();
    return info;
#else
    return std::nullopt;
#endif
}

// ---------------------------------------------------------------------------
//  Aggregate
// ---------------------------------------------------------------------------

inline std::vector<IDEInfo> detect_all() {
    std::vector<IDEInfo> out;
    if (auto v = detect_vscode(false))      out.push_back(*v);
    if (auto v = detect_vscode(true))       out.push_back(*v);
    for (auto& vs : detect_visual_studio()) out.push_back(std::move(vs));
    if (auto v = detect_clion())            out.push_back(*v);
    if (auto v = detect_rider())            out.push_back(*v);
    if (auto v = detect_rustrover())        out.push_back(*v);
    if (auto v = detect_intellij())         out.push_back(*v);
    if (auto v = detect_qt_creator())       out.push_back(*v);
    return out;
}

inline bool is_installed(IDE id) {
    switch (id) {
        case IDE::VSCode:           return detect_vscode(false).has_value();
        case IDE::VSCodeInsiders:   return detect_vscode(true).has_value();
        case IDE::VisualStudio:     return !detect_visual_studio().empty();
        case IDE::CLion:            return detect_clion().has_value();
        case IDE::Rider:            return detect_rider().has_value();
        case IDE::RustRover:        return detect_rustrover().has_value();
        case IDE::IntelliJ:         return detect_intellij().has_value();
        case IDE::QtCreator:        return detect_qt_creator().has_value();
        default:                    return false;
    }
}

// ---------------------------------------------------------------------------
//  Launch
// ---------------------------------------------------------------------------

// Open a project folder, .sln, or single file in the given IDE.
// For VS Code we pass the path as a positional arg (works for files and folders).
// For Visual Studio we use /Edit for files and direct arg for .sln/.csproj/etc.
// For JetBrains IDEs the path arg is always accepted.
inline bool open_in_ide(const IDEInfo& ide, const fs::path& target) {
#ifdef _WIN32
    std::string args;
    if (ide.id == IDE::VisualStudio) {
        std::error_code ec;
        bool is_solution = target.has_extension() &&
            (target.extension() == ".sln" || target.extension() == ".vcxproj" ||
             target.extension() == ".csproj" || fs::is_directory(target, ec));
        if (!is_solution && fs::is_regular_file(target, ec)) {
            args = "/Edit \"" + target.string() + "\"";
        } else {
            args = "\"" + target.string() + "\"";
        }
    } else {
        args = "\"" + target.string() + "\"";
    }
    HINSTANCE rc = ShellExecuteA(nullptr, "open",
                                 ide.executable.string().c_str(),
                                 args.c_str(),
                                 nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(rc) > 32;
#else
    std::string cmd = "\"" + ide.executable.string() + "\" \"" + target.string() + "\" &";
    return std::system(cmd.c_str()) == 0;
#endif
}

// Open a specific file with line/column (VS Code only supports this nicely).
inline bool open_at_location(const IDEInfo& ide,
                              const fs::path& file,
                              int line = -1, int column = -1) {
#ifdef _WIN32
    if (ide.id == IDE::VSCode || ide.id == IDE::VSCodeInsiders) {
        std::string loc = file.string();
        if (line > 0) {
            loc += ":" + std::to_string(line);
            if (column > 0) loc += ":" + std::to_string(column);
        }
        std::string args = "--goto \"" + loc + "\"";
        HINSTANCE rc = ShellExecuteA(nullptr, "open",
                                     ide.executable.string().c_str(),
                                     args.c_str(), nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(rc) > 32;
    }
#endif
    (void)line; (void)column;
    return open_in_ide(ide, file);
}

} // namespace ide_helper
