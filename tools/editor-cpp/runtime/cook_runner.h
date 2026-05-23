#pragma once

// CookRunner — threaded cook driver (Phase 5a). Calls the engine's `cook()`
// API (`code/sys/sys_cook2.h`) in a background thread on a file list.
// Two modes:
//   - In-Place: `cook(absPath)` for every file — the engine creates `.foo.png`
//                cache files (for recipe-masks, e.g. ffmpeg-audio).
//   - Build cook.zip: same + `zip_append_mem` into an archive file.
//
// Threading:
//   - Only ONE worker can run at a time (the engine's cook() is NOT
//     thread-safe: global `rules`, `static` locals). Guarded by `running_` atomic.
//   - Per-asset progress: the worker sends the events to the main thread via
//     `mainQueue.enqueue(...)` (kEvtCookStarted/Progress/Finished/Cancelled).
//   - Cancel: `cancel_requested_=true` → breaks at the start of the next cycle
//     (the running `cook(path)` finishes, mid-file cannot be interrupted).

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace editor {

class EditorApp;
class MainThreadQueue;

class CookRunner {
public:
    CookRunner(EditorApp& app, MainThreadQueue& q);
    ~CookRunner();

    CookRunner(const CookRunner&)            = delete;
    CookRunner& operator=(const CookRunner&) = delete;

    // Cook in-place: cook() call for every absPath. The engine creates the
    // recipe-based `.foo.png` cache files. Returns true if started, false
    // if another cook is already running.
    bool startCookInPlace(std::vector<std::string> absPaths);

    // Cook + cook.zip build. zipPath = output abs path (defaults to
    // `<projectPath>/cook.zip`). The engine recognizes it at startup (`cook_zip`).
    bool startBuildZip(std::vector<std::string> absPaths,
                       std::string              zipPath);

    void requestCancel() { cancel_requested_.store(true); }
    void joinIfDone();                  // if done, join() — release thread

    bool isRunning()       const { return running_.load(); }
    int  progressCurrent() const { return progress_current_.load(); }
    int  progressTotal()   const { return progress_total_.load(); }

private:
    void workerInPlace(std::vector<std::string> paths);
    void workerZip(std::vector<std::string> paths, std::string zip);

    EditorApp&        app_;
    MainThreadQueue&  queue_;
    std::thread       worker_;
    std::atomic<int>  progress_current_{0};
    std::atomic<int>  progress_total_{0};
    std::atomic<bool> cancel_requested_{false};
    std::atomic<bool> running_{false};
};

}  // namespace editor
