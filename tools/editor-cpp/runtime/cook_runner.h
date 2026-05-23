#pragma once

// CookRunner — threaded cook driver (Phase 5a). A motor `cook()` API-ját
// (`code/sys/sys_cook2.h`) hívja egy background-thread-ben fájl-listára.
// Két mód:
//   - In-Place: minden file-ra `cook(absPath)` — a motor `.foo.png` cache-
//                fájlokat hoz létre (a recipe-mask-okhoz, pl. ffmpeg-audio).
//   - Build cook.zip: ugyanaz + `zip_append_mem` egy archív-fájlba.
//
// Threading:
//   - Egyszerre csak EGY worker futhat (a motor cook() NEM thread-safe:
//     globális `rules`, `static` lokálok). `running_` atomic őrzi.
//   - Progress per-asset: a worker `mainQueue.enqueue(...)`-pal főszálba
//     küldi az event-eket (kEvtCookStarted/Progress/Finished/Cancelled).
//   - Cancel: `cancel_requested_=true` → a következő ciklus elején break-el
//     (a futó `cook(path)` befejeződik, mid-file nem szakítható meg).

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

    // Cook in-place: minden absPath-re cook() hívás. A motor a recipe-szerinti
    // `.foo.png` cache-fájlokat hozza létre. Visszaad true ha indult, false
    // ha már fut egy másik cook.
    bool startCookInPlace(std::vector<std::string> absPaths);

    // Cook + cook.zip build. zipPath = output abs path (alapból
    // `<projectPath>/cook.zip`). A motor induláskor felismeri (`cook_zip`).
    bool startBuildZip(std::vector<std::string> absPaths,
                       std::string              zipPath);

    void requestCancel() { cancel_requested_.store(true); }
    void joinIfDone();                  // ha lefutott, join() — release thread

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
