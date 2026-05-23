#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace editor {

// Közös helper a panel-cache-ekhez. Mtime-poll + failedPaths timeout:
//
//   - `mtimeNs(path)` — std::filesystem::last_write_time → uint64_t ns.
//   - `shouldRetryFailed(path)` — true ha a path failedPaths-ban volt, de
//     a timeout lejárt (alapból 2 másodperc). Use-case: a felhasználó a
//     lemezen javítja a hibás asset-et — az editor magától újra-próbálja.
//
// A panel-cache (model_t / texture_t / tiled_t) egyszerűbben használja:
//
//   AssetMtimes mtimes;       // path → utolsó látott mtime
//   FailedPathSet failed;     // path → mikor adta fel
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
    // Timeout (másodperc) — alapból 2.0s után újra-próba.
    double retry_after = 2.0;

    void insert(const std::string& path) { entries_[path] = nowSeconds(); }
    void erase(const std::string& path) { entries_.erase(path); }

    // true ha bent van ÉS még friss (NEM kell újra próbálkozni).
    bool isFresh(const std::string& path) const {
        auto it = entries_.find(path);
        if (it == entries_.end()) return false;
        return (nowSeconds() - it->second) < retry_after;
    }

    // true ha érdemes újra-próbálkozni (vagy nem volt benn, vagy lejárt).
    bool shouldRetry(const std::string& path) const {
        return !isFresh(path);
    }

private:
    std::unordered_map<std::string, double> entries_;
};

using AssetMtimes = std::unordered_map<std::string, uint64_t>;

}  // namespace editor
