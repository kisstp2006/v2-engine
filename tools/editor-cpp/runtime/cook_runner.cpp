// STL ELŐSZÖR.
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "cook_runner.h"
#include "../app/editor_app.h"
#include "../core/asset_path.h"
#include "../core/event_bus.h"
#include "../core/events.h"
#include "../core/task_queue.h"

namespace editor {

namespace {

// Az ext-cook/demo.c-ből vett konvenció: ULZ tömörítés, level 1.
// (`code/obj/obj_pack_compress.h`: COMPRESS_ULZ = 2<<4 = 32.)
constexpr unsigned kZipCompressFlags = (2u << 4) | 1u;

// A `cook()` cache-key egy `.<filename>` rejtett fájl. NE cookoljuk újra
// a már cookolt fájlokat (mint az ext-cook/demo.c is csinálja).
bool isHiddenCookedFile(const std::string& absPath) {
    namespace fs = std::filesystem;
    std::string name = fs::path(absPath).filename().string();
    return !name.empty() && name[0] == '.';
}

}  // namespace

CookRunner::CookRunner(EditorApp& app, MainThreadQueue& q)
    : app_(app), queue_(q) {}

CookRunner::~CookRunner() {
    cancel_requested_.store(true);
    if (worker_.joinable()) worker_.join();
}

void CookRunner::joinIfDone() {
    if (!running_.load() && worker_.joinable()) {
        worker_.join();
    }
}

bool CookRunner::startCookInPlace(std::vector<std::string> absPaths) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return false;       // már fut egy cook
    }
    if (worker_.joinable()) worker_.join();   // előző join (ha kimaradt)
    cancel_requested_.store(false);
    progress_current_.store(0);
    progress_total_.store((int)absPaths.size());

    worker_ = std::thread(&CookRunner::workerInPlace, this,
                          std::move(absPaths));
    return true;
}

bool CookRunner::startBuildZip(std::vector<std::string> absPaths,
                               std::string              zipPath) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return false;
    }
    if (worker_.joinable()) worker_.join();
    cancel_requested_.store(false);
    progress_current_.store(0);
    progress_total_.store((int)absPaths.size());

    worker_ = std::thread(&CookRunner::workerZip, this,
                          std::move(absPaths), std::move(zipPath));
    return true;
}

// ---- Worker-side implementáció --------------------------------------------

void CookRunner::workerInPlace(std::vector<std::string> paths) {
    const std::string projectRoot = app_.projectPath();

    // Start event.
    {
        CookProgress p{0, (int)paths.size(), {}};
        queue_.enqueue([this, p]() {
            app_.bus().emit(kEvtCookStarted, p);
            app_.bus().emit(kEvtLogInfo,
                std::string("[Cook] started: ") + std::to_string(p.total) +
                " files (in-place)");
        });
    }

    int succeeded = 0, failed = 0;
    bool cancelled = false;
    for (size_t i = 0; i < paths.size(); ++i) {
        if (cancel_requested_.load()) { cancelled = true; break; }

        const std::string& abs = paths[i];
        if (isHiddenCookedFile(abs)) {
            ++succeeded;
            progress_current_.store((int)i + 1);
            continue;
        }

        // A motor `cook()` szinkron blokkoló, NEM thread-safe — de mi
        // garantáljuk hogy egyszerre csak egy worker fut (running_ atomic).
        const char* out = cook(abs.c_str());
        const bool ok = (out != nullptr);
        if (ok) ++succeeded; else ++failed;

        progress_current_.store((int)i + 1);

        std::string rel = asset_path::toProjectRelative(abs, projectRoot);
        CookProgress p{ (int)i + 1, (int)paths.size(), rel };
        queue_.enqueue([this, p, ok]() {
            app_.bus().emit(kEvtCookProgress, p);
            app_.bus().emit(ok ? kEvtLogInfo : kEvtLogWarn,
                std::string(ok ? "[Cook] ok: " : "[Cook] fail: ") +
                p.currentFile);
        });
    }

    // Finished/cancelled event.
    CookResult res;
    res.succeeded = succeeded;
    res.failed    = failed;
    res.total     = (int)paths.size();
    res.cancelled = cancelled;
    queue_.enqueue([this, res, cancelled]() {
        app_.bus().emit(cancelled ? kEvtCookCancelled : kEvtCookFinished, res);
        app_.bus().emit(kEvtLogInfo,
            std::string("[Cook] ") + (cancelled ? "cancelled" : "done") +
            ": " + std::to_string(res.succeeded) + " ok, " +
            std::to_string(res.failed) + " fail" +
            (cancelled ? std::string(" (") +
                std::to_string(res.succeeded + res.failed) + "/" +
                std::to_string(res.total) + ")" : std::string()));
    });

    running_.store(false);
}

void CookRunner::workerZip(std::vector<std::string> paths,
                           std::string              zipPath) {
    const std::string projectRoot = app_.projectPath();

    // zip_open "wb" → új zip vagy felülírja a meglévőt.
    zip_t* z = zip_open(zipPath.c_str(), "wb");
    if (!z) {
        queue_.enqueue([this, zipPath]() {
            app_.bus().emit(kEvtLogError,
                std::string("[Cook] zip_open failed: ") + zipPath);
            CookResult r; r.outputPath = zipPath;
            app_.bus().emit(kEvtCookFinished, r);
        });
        running_.store(false);
        return;
    }

    {
        CookProgress p{0, (int)paths.size(), {}};
        queue_.enqueue([this, p, zipPath]() {
            app_.bus().emit(kEvtCookStarted, p);
            app_.bus().emit(kEvtLogInfo,
                std::string("[Cook] started: ") + std::to_string(p.total) +
                " files → " + zipPath);
        });
    }

    int succeeded = 0, failed = 0;
    bool cancelled = false;
    for (size_t i = 0; i < paths.size(); ++i) {
        if (cancel_requested_.load()) { cancelled = true; break; }

        const std::string& abs = paths[i];
        if (isHiddenCookedFile(abs)) {
            ++succeeded;
            progress_current_.store((int)i + 1);
            continue;
        }

        // file_read() triggereli a motor automata-cook-ot (file_handle()
        // belül cook(path)-t hív, és ha van recipe → a cookolt tartalmat
        // adja vissza). Tehát itt nem kell külön cook()-ot hívni.
        int size = 0;
        char* data = file_read(abs.c_str(), &size);
        bool ok = (data != nullptr && size >= 0);
        if (ok) {
            // Zip-ben relatív path-ot tárolunk (a runtime is így keresi).
            std::string rel = asset_path::toProjectRelative(abs, projectRoot);
            ok = zip_append_mem(z, rel.c_str(), "", data, (unsigned)size,
                                kZipCompressFlags);
        }
        if (ok) ++succeeded; else ++failed;
        progress_current_.store((int)i + 1);

        std::string rel = asset_path::toProjectRelative(abs, projectRoot);
        CookProgress p{ (int)i + 1, (int)paths.size(), rel };
        queue_.enqueue([this, p, ok]() {
            app_.bus().emit(kEvtCookProgress, p);
            app_.bus().emit(ok ? kEvtLogInfo : kEvtLogWarn,
                std::string(ok ? "[Cook] zip+: " : "[Cook] zip fail: ") +
                p.currentFile);
        });
    }

    zip_close(z);

    CookResult res;
    res.succeeded  = succeeded;
    res.failed     = failed;
    res.total      = (int)paths.size();
    res.outputPath = zipPath;
    res.cancelled  = cancelled;
    queue_.enqueue([this, res, cancelled]() {
        app_.bus().emit(cancelled ? kEvtCookCancelled : kEvtCookFinished, res);
        app_.bus().emit(kEvtLogInfo,
            std::string("[Cook] ") + (cancelled ? "cancelled" : "done") +
            " → " + res.outputPath + " (" +
            std::to_string(res.succeeded) + " ok, " +
            std::to_string(res.failed) + " fail)");
    });

    running_.store(false);
}

}  // namespace editor
