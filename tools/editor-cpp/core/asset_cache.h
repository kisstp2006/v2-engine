#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace editor {

// Common helper for panel-caches. Mtime-poll + failedPaths timeout:
//
//   - `mtimeNs(path)` — std::filesystem::last_write_time → uint64_t ns.
//   - `shouldRetryFailed(path)` — true if path was in failedPaths but the
//     timeout has expired (2 seconds by default). Use-case: the user
//     fixes the broken asset on disk — the editor retries on its own.
//
// The panel-cache (model_t / texture_t / tiled_t) uses it more simply:
//
//   AssetMtimes mtimes;       // path → last seen mtime
//   FailedPathSet failed;     // path → when it gave up
//   if (failed.shouldRetry(path)) failed.erase(path);
//   uint64_t now_mtime = mtimeNs(path);
//   if (mtimes[path] != now_mtime) { cache.erase(path); mtimes[path] = now_mtime; }
//
// Refaktor F1 note: implementation moved to asset_cache.cpp so this header
// no longer drags in <filesystem>/<chrono>. The motor's engine.h macros
// (`set`, `obj`, `is`, ...) collide with <xlocale> internals when STL is
// included AFTER engine.h, so keeping this header STL-light lets dependents
// include it from any spot in the include chain.

uint64_t mtimeNs(const std::string& path);
double   nowSeconds();

class FailedPathSet {
public:
    // Timeout (seconds) — retry after 2.0s by default.
    double retry_after = 2.0;

    void insert(const std::string& path);
    void erase(const std::string& path);

    // true if inside AND still fresh (NO retry needed).
    bool isFresh(const std::string& path) const;

    // true if worth retrying (either wasn't in, or expired).
    bool shouldRetry(const std::string& path) const;

private:
    std::unordered_map<std::string, double> entries_;
};

using AssetMtimes = std::unordered_map<std::string, uint64_t>;

}  // namespace editor
