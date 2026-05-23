// STL FIRST.
#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

#include "asset_path.h"

namespace editor::asset_path {

namespace {

namespace fs = std::filesystem;

// Backslash → forward slash in the string.
std::string normSlash(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

// `weakly_canonical` — lexically_normal form without resolving symlinks.
// Works even if the file does not exist (`canonical` throws in that case).
fs::path normalize(const fs::path& p) {
    std::error_code ec;
    fs::path n = fs::weakly_canonical(p, ec);
    return ec ? p.lexically_normal() : n;
}

}  // namespace

bool isAbsolute(const std::string& path) {
    if (path.empty()) return false;
    return fs::path(path).is_absolute();
}

bool isWithinProject(const std::string& abs,
                     const std::string& projectRoot) {
    if (abs.empty() || projectRoot.empty()) return false;
    fs::path absN  = normalize(fs::path(abs));
    fs::path rootN = normalize(fs::path(projectRoot));

    auto itA = absN.begin();
    auto itR = rootN.begin();
    for (; itR != rootN.end(); ++itR, ++itA) {
        if (itA == absN.end()) return false;
        if (*itA != *itR) return false;
    }
    return true;
}

std::string toProjectRelative(const std::string& abs,
                              const std::string& projectRoot) {
    if (abs.empty()) return {};
    if (projectRoot.empty()) return normSlash(abs);
    if (!isWithinProject(abs, projectRoot)) return normSlash(abs);

    std::error_code ec;
    fs::path rel = fs::relative(normalize(fs::path(abs)),
                                normalize(fs::path(projectRoot)), ec);
    if (ec) return normSlash(abs);
    return normSlash(rel.string());
}

std::string toAbsolute(const std::string& rel,
                       const std::string& projectRoot) {
    if (rel.empty()) return {};
    fs::path p(rel);
    if (p.is_absolute()) return normSlash(p.string());
    if (projectRoot.empty()) return normSlash(p.string());

    fs::path full = fs::path(projectRoot) / p;
    return normSlash(normalize(full).string());
}

}  // namespace editor::asset_path
