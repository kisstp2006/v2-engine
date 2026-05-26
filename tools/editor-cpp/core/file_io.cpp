// STL FIRST — <filesystem> drags <xlocale> which clashes with the motor's
// `is`/`set`/`obj` macros if engine.h has already been included. Keep this
// translation unit pure-STL; no engine.h here.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "file_io.h"

namespace fs = std::filesystem;

namespace editor::file_io {

namespace {

// Internal — backslash → forward-slash. STL fstream accepts both on Windows,
// but normalizing keeps logs / cache keys / error messages consistent.
std::string normalize(const std::string& path) {
    std::string out = path;
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

// Create the parent directory chain (idempotent). Returns true on success
// OR when the dir already exists. False on real filesystem errors.
bool ensureParentDirs(const fs::path& target) {
    if (!target.has_parent_path()) return true;
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    // create_directories returns false if the dir already exists, but only
    // sets ec on a real error. So inspect ec, not the return value.
    return !ec;
}

}  // namespace

// ---------------------------------------------------------------------------
// Read
// ---------------------------------------------------------------------------

std::string readText(const std::string& path) {
    std::ifstream f(normalize(path), std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::vector<uint8_t> readBytes(const std::string& path) {
    std::ifstream f(normalize(path), std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamsize sz = f.tellg();
    if (sz <= 0) return {};
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    if (!f.read(reinterpret_cast<char*>(buf.data()), sz)) return {};
    return buf;
}

// ---------------------------------------------------------------------------
// Write (atomic via .tmp + rename)
// ---------------------------------------------------------------------------

bool writeText(const std::string& path, const std::string& content) {
    return writeBytes(path, content.data(), content.size());
}

bool writeBytes(const std::string& path, const void* data, size_t size) {
    fs::path target(normalize(path));
    if (!ensureParentDirs(target)) return false;

    fs::path tmp = target;
    tmp += ".tmp";

    // Scoped ofstream so the file is closed (and flushed) before rename.
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        if (size > 0 && data != nullptr) {
            f.write(reinterpret_cast<const char*>(data),
                    static_cast<std::streamsize>(size));
            if (!f.good()) return false;
        }
        f.flush();
        if (!f.good()) return false;
    }

    // fs::rename: on Windows, fails if the destination exists. Retry pattern:
    // try rename → on EEXIST, remove dest then rename again. Atomic-ish:
    // a crash between remove and rename leaves <target>.tmp on disk; the
    // next save fixes it.
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (!ec) return true;

    std::error_code ec2;
    fs::remove(target, ec2);
    // Ignore ec2 — if remove fails because target doesn't exist, the next
    // rename succeeds. If it fails for another reason, the next rename
    // will also fail and we'll return false.
    std::error_code ec3;
    fs::rename(tmp, target, ec3);
    if (!ec3) return true;

    // Last-resort cleanup: try to remove the orphaned .tmp.
    std::error_code ec4;
    fs::remove(tmp, ec4);
    return false;
}

// ---------------------------------------------------------------------------
// Existence + metadata
// ---------------------------------------------------------------------------

bool exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(normalize(path), ec);
}

bool isFile(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(normalize(path), ec);
}

bool isDirectory(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(normalize(path), ec);
}

uint64_t mtimeNs(const std::string& path) {
    std::error_code ec;
    auto t = fs::last_write_time(normalize(path), ec);
    if (ec) return 0;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            t.time_since_epoch()).count());
}

// ---------------------------------------------------------------------------
// Directory listing
// ---------------------------------------------------------------------------

std::vector<std::string> listFiles(const std::string& dir,
                                   const std::string& ext_filter) {
    std::vector<std::string> out;
    std::error_code ec;
    std::string norm = normalize(dir);
    if (!fs::is_directory(norm, ec)) return out;

    for (const auto& e : fs::directory_iterator(norm, ec)) {
        if (!e.is_regular_file()) continue;
        if (!ext_filter.empty() && e.path().extension() != ext_filter) continue;
        out.push_back(e.path().generic_string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace editor::file_io
