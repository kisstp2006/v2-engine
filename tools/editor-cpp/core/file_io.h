#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Cross-platform file I/O for the editor. Built on <fstream> + <filesystem>
// — does NOT route through the motor's `file_read()` / `file_write()`.
//
// Why a separate I/O layer:
//   The motor's file_read() chains is_file() → cook() → fopen(). On certain
//   Windows configurations (OneDrive Documents online-only files, mixed
//   slash paths, projects under "controlled folder access") this chain
//   silently fails on files that DO exist on disk and ARE readable via the
//   regular Windows file APIs. The result was a per-frame retry loop in
//   the material override pipeline ("file_read cannot find" × 30000 lines
//   in 20 seconds, editor unresponsive).
//
//   STL <fstream> + <filesystem> use the platform-correct path resolution
//   (UTF-8 → UTF-16 on Windows) and work on every path the OS itself can
//   reach via Explorer.
//
// What's here:
//   - readText / readBytes        (one-shot full-file read)
//   - writeText / writeBytes      (atomic: write to .tmp + rename)
//   - exists / isFile / isDirectory / mtimeNs
//   - listFiles                   (one level, optional extension filter)
//
// Header is STL-light on purpose: no <filesystem> include here, so callers
// can include this from any spot in their include chain (the motor's
// engine.h macros clash with <xlocale> internals if STL is pulled in after).
//
// Thread: main-thread only, like everything else in the editor.

namespace editor::file_io {

// Read a file's full text content. Returns empty string on any error.
// To distinguish "empty file" from "missing file", call exists() / isFile()
// first. Backslash and forward-slash both accepted; internally normalized.
std::string readText(const std::string& path);

// Same as readText but returns raw bytes (no null-termination assumption).
std::vector<uint8_t> readBytes(const std::string& path);

// Atomic write: writes to `<path>.tmp` first, then renames to `<path>`.
// Creates parent directories on demand. Returns true on success.
bool writeText (const std::string& path, const std::string& content);
bool writeBytes(const std::string& path, const void* data, size_t size);

// Existence + metadata via <filesystem>. All cross-platform; none use the
// motor's stat() chain.
bool      exists      (const std::string& path);
bool      isFile      (const std::string& path);
bool      isDirectory (const std::string& path);

// File mtime in nanoseconds since the filesystem's reference epoch. Returns 0
// on error. Suitable for change-detection (compare equal/not-equal between
// calls); not for human-readable dates.
uint64_t  mtimeNs     (const std::string& path);

// List FILES (regular files only, no subdirs) directly inside `dir`. Returns
// ABSOLUTE forward-slash-normalized paths, sorted alphabetically. If
// `ext_filter` is non-empty, only files whose extension matches are returned
// (include the dot: ".glsl", ".json5", etc.).
std::vector<std::string> listFiles(const std::string& dir,
                                   const std::string& ext_filter = "");

}  // namespace editor::file_io
