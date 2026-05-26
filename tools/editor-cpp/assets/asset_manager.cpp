// STL FIRST.
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "asset_manager.h"

#include "../core/asset_path.h"
#include "../core/event_bus.h"
#include "../core/events.h"
#include "../core/file_io.h"
#include "../core/texture_loader.h"

namespace editor {

AssetManager::AssetManager(EventBus& bus, const std::string& projectPath)
    : bus_(&bus), projectRoot_(projectPath) {
    wireBus_();
}

AssetManager::~AssetManager() {
    // Skyboxes own GL cubemap textures; the motor's other assets (model_t,
    // texture_t, tiled_t) get cleaned up at process exit. Mirrors the
    // pre-refactor scene_panel/game_panel destructor behavior exactly.
    for (auto& kv : skyboxes_) skybox_destroy(&kv.second);
    skyboxes_.clear();
    skyboxMtimes_.clear();
}

void AssetManager::wireBus_() {
    // Any scene mutation invalidates the per-obj* pathCache_ (the obj*s
    // themselves may be freed). Cleanup is cheap, run on both events.
    auto clear_cb = [this](const std::any&) { onSceneMutated(); };
    bus_->on(kEvtSceneReplaced, clear_cb);
    bus_->on(kEvtSceneDirty,    clear_cb);
}

void AssetManager::onSceneMutated() {
    pathCache_.clear();
}

const std::string& AssetManager::absPathFor(obj* node, const std::string& relPath) {
    auto it = pathCache_.find(node);
    if (it != pathCache_.end() && it->second.rel == relPath) {
        return it->second.abs;
    }
    // asset_path::toAbsolute() calls fs::weakly_canonical() — a ~170 µs
    // Windows syscall. Cache it per obj*.
    std::string abs = ::editor::asset_path::toAbsolute(relPath, projectRoot_);
    auto [ins_it, _] = pathCache_.insert_or_assign(
        node, PathCacheEntry{relPath, std::move(abs)});
    return ins_it->second.abs;
}

// -- Model --------------------------------------------------------------------

model_t* AssetManager::loadModel(const std::string& absPath,
                                 const std::string& relPathForLog) {
    if (absPath.empty()) return nullptr;

    // 1. failedPaths timeout — if the user moved/deleted recently, skip.
    if (failedPaths_.isFresh(absPath)) return nullptr;

    // 2. is_file check only when not yet cached (cached == known to exist).
    //    This mirrors the perf optimization from scene_panel.cpp 307-319.
    if (models_.find(absPath) == models_.end()) {
        failedPaths_.erase(absPath);
        if (!is_file(absPath.c_str())) {
            failedPaths_.insert(absPath);
            bus_->emit(kEvtLogInfo,
                std::string("[Mesh] file not found: ") + relPathForLog);
            return nullptr;
        }
    } else {
        failedPaths_.erase(absPath);
    }

    // 3. mtime-poll → drop cache entry if file changed on disk.
    uint64_t mt_now = mtimeNs(absPath);
    auto mt_it = modelMtimes_.find(absPath);
    if (mt_it != modelMtimes_.end() && mt_it->second != mt_now) {
        models_.erase(absPath);
        bus_->emit(kEvtLogInfo,
            std::string("[Mesh] reloaded (mtime): ") + relPathForLog);
    }

    // 4. Cache hit → return; else load + insert.
    auto it = models_.find(absPath);
    if (it == models_.end()) {
        // Motor `model(path, flags)` would internally do `file_read(path)` +
        // `model_from_mem(buf, len, flags)`. The motor file_read fails on
        // certain Windows path edge-cases (filenames with spaces, OneDrive
        // online-only files, etc.). Replicate the load via editor::file_io +
        // model_from_mem directly so the load works on every Windows path.
        std::vector<uint8_t> bytes = editor::file_io::readBytes(absPath);
        if (bytes.empty()) {
            failedPaths_.insert(absPath);
            bus_->emit(kEvtLogInfo,
                std::string("[Mesh] read failed: ") + relPathForLog);
            return nullptr;
        }
        // model_from_mem internals (IQM: memcpy into q->buf; GLTF: cgltf_parse
        // + cgltf_free in the same call → mesh data already in VBO/IBO before
        // the function returns) do not retain a pointer to `mem` past the
        // call. Still, mirror the audio()/tiled() STRDUP convention so we are
        // robust against future motor changes — models are rare per project,
        // so the heap copy is negligible.
        char* contentHeap = (char*)MALLOC(bytes.size());
        memcpy(contentHeap, bytes.data(), bytes.size());
        model_t mt = model_from_mem(contentHeap, (int)bytes.size(), 0);
        if (!mt.iqm) {
            FREE(contentHeap);
            failedPaths_.insert(absPath);
            bus_->emit(kEvtLogInfo,
                std::string("[Mesh] load failed: ") + relPathForLog);
            return nullptr;
        }
        // Motor `model()` STRDUPs the filename onto `mt.filename`; replicate
        // so downstream code (which logs / compares filenames) gets the
        // expected absolute-path string.
        mt.filename = STRDUP(absPath.c_str());
        auto ins = models_.emplace(absPath, mt);
        it = ins.first;
        modelMtimes_[absPath] = mt_now;
        bus_->emit(kEvtLogInfo,
            std::string("[Mesh] loaded: ") + relPathForLog);
    }
    return &it->second;
}

model_t* AssetManager::modelByAbsPath(const std::string& absPath) {
    auto it = models_.find(absPath);
    return (it == models_.end()) ? nullptr : &it->second;
}

// -- Skybox -------------------------------------------------------------------

skybox_t* AssetManager::loadSkybox(const std::string& absSkyPath,
                                   const std::string& absReflPath,
                                   const std::string& absEnvPath) {
    if (absSkyPath.empty()) return nullptr;
    if (!is_file(absSkyPath.c_str())) return nullptr;

    uint64_t mt_now = mtimeNs(absSkyPath);
    auto mt_it = skyboxMtimes_.find(absSkyPath);
    auto it    = skyboxes_.find(absSkyPath);

    if (it != skyboxes_.end() && mt_it != skyboxMtimes_.end() &&
        mt_it->second != mt_now) {
        skybox_destroy(&it->second);
        skyboxes_.erase(it);
        it = skyboxes_.end();
    }
    if (it == skyboxes_.end()) {
        // refl / env fall back to the sky path when callers leave them empty.
        const char* refl = absReflPath.empty() ? absSkyPath.c_str()
                                               : absReflPath.c_str();
        const char* env  = absEnvPath.empty()  ? absSkyPath.c_str()
                                               : absEnvPath.c_str();
        skybox_t sky = skybox(absSkyPath.c_str(), refl, env);
        auto ins = skyboxes_.emplace(absSkyPath, sky);
        it = ins.first;
        skyboxMtimes_[absSkyPath] = mt_now;
    }
    return &it->second;
}

// -- Texture ------------------------------------------------------------------

texture_t* AssetManager::loadTexture(const std::string& absPath,
                                     const std::string& relPathForLog) {
    if (absPath.empty()) return nullptr;
    if (failedPaths_.isFresh(absPath)) return nullptr;

    if (textures_.find(absPath) == textures_.end()) {
        failedPaths_.erase(absPath);
        if (!is_file(absPath.c_str())) {
            failedPaths_.insert(absPath);
            bus_->emit(kEvtLogInfo,
                std::string("[Texture] file not found: ") + relPathForLog);
            return nullptr;
        }
    } else {
        failedPaths_.erase(absPath);
    }

    uint64_t mt_now = mtimeNs(absPath);
    auto mt_it = textureMtimes_.find(absPath);
    if (mt_it != textureMtimes_.end() && mt_it->second != mt_now) {
        textures_.erase(absPath);
        bus_->emit(kEvtLogInfo,
            std::string("[Texture] reloaded (mtime): ") + relPathForLog);
    }

    auto it = textures_.find(absPath);
    if (it == textures_.end()) {
        // Route through the unified editor::texture_loader — same code path
        // that material_asset_io / material_drop_target use. STRDUP'd
        // filename + GPU upload all happen inside loadCompressed().
        texture_t tx = editor::texture_loader::loadCompressed(absPath, 0);
        if (!tx.id) {
            failedPaths_.insert(absPath);
            bus_->emit(kEvtLogInfo,
                std::string("[Texture] load failed: ") + relPathForLog);
            return nullptr;
        }
        auto ins = textures_.emplace(absPath, tx);
        it = ins.first;
        textureMtimes_[absPath] = mt_now;
        bus_->emit(kEvtLogInfo,
            std::string("[Texture] loaded: ") + relPathForLog);
    }
    return &it->second;
}

// -- Tilemap ------------------------------------------------------------------

tiled_t* AssetManager::loadTilemap(const std::string& absPath,
                                   const std::string& relPathForLog) {
    if (absPath.empty()) return nullptr;
    if (failedPaths_.isFresh(absPath)) return nullptr;

    if (tilemaps_.find(absPath) == tilemaps_.end()) {
        failedPaths_.erase(absPath);
        if (!is_file(absPath.c_str())) {
            failedPaths_.insert(absPath);
            bus_->emit(kEvtLogInfo,
                std::string("[Tilemap] file not found: ") + relPathForLog);
            return nullptr;
        }
    } else {
        failedPaths_.erase(absPath);
    }

    uint64_t mt_now = mtimeNs(absPath);
    auto mt_it = tilemapMtimes_.find(absPath);
    if (mt_it != tilemapMtimes_.end() && mt_it->second != mt_now) {
        tilemaps_.erase(absPath);
        bus_->emit(kEvtLogInfo,
            std::string("[Tilemap] reloaded (mtime): ") + relPathForLog);
    }

    auto it = tilemaps_.find(absPath);
    if (it == tilemaps_.end()) {
        // `tiled()` expects TMX content, not the path. Use editor::file_io
        // (STL fstream) instead of the motor's file_read — robust against
        // Windows path-edge-cases (OneDrive online-only files, etc.).
        std::string content = editor::file_io::readText(absPath);
        if (content.empty()) {
            failedPaths_.insert(absPath);
            bus_->emit(kEvtLogInfo,
                std::string("[Tilemap] read failed: ") + relPathForLog);
            return nullptr;
        }
        // tiled() may keep a pointer into the buffer past the parse call
        // (the pre-refactor code relied on the motor's STRDUP-like slot
        // buffer staying alive). STRDUP to a heap copy that lives as long
        // as the cache entry. Tilemaps are rare per project, so the leak
        // (no destroy hook in motor) is negligible.
        char* mutbuf = STRDUP(content.c_str());
        tiled_t tm = tiled(mutbuf);
        // first_gid == 0 indicates a parse failure (scene_panel_2d convention).
        if (!tm.first_gid) {
            failedPaths_.insert(absPath);
            bus_->emit(kEvtLogInfo,
                std::string("[Tilemap] parse failed: ") + relPathForLog);
            return nullptr;
        }
        auto ins = tilemaps_.emplace(absPath, tm);
        it = ins.first;
        tilemapMtimes_[absPath] = mt_now;
        bus_->emit(kEvtLogInfo,
            std::string("[Tilemap] loaded: ") + relPathForLog);
    }
    return &it->second;
}

}  // namespace editor
