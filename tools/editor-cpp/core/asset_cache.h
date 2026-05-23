#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
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

inline uint64_t mtimeNs(const std::string& path) {
    std::error_code ec;
    auto t = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        t.time_since_epoch()).count();
}

inline double nowSeconds() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(
        steady_clock::now().time_since_epoch()).count();
}

class FailedPathSet {
public:
    // Timeout (seconds) — retry after 2.0s by default.
    double retry_after = 2.0;

    void insert(const std::string& path) { entries_[path] = nowSeconds(); }
    void erase(const std::string& path) { entries_.erase(path); }

    // true if inside AND still fresh (NO retry needed).
    bool isFresh(const std::string& path) const {
        auto it = entries_.find(path);
        if (it == entries_.end()) return false;
        return (nowSeconds() - it->second) < retry_after;
    }

    // true if worth retrying (either wasn't in, or expired).
    bool shouldRetry(const std::string& path) const {
        return !isFresh(path);
    }

private:
    std::unordered_map<std::string, double> entries_;
};

using AssetMtimes = std::unordered_map<std::string, uint64_t>;

}  // namespace editor
