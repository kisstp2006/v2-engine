// STL + ide_helper FIRST (the engine's `ifdef_*` macros corrupt
// `<filesystem>` in the stdlib headers — pure-STL must come first).
#include <chrono>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>

#include "../core/ide_launcher.h"

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include <fstream>
#include <sstream>

#include "asset_preview.h"
#include "../app/editor_app.h"
#include "../app/file_type_registry.h"
#include "../commands/command.h"
#include "../core/asset_path.h"
#include "../core/event_bus.h"
#include "../core/events.h"
#include "../persistence/material_asset_io.h"
#include "material_drawer.h"

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <shellapi.h>
#endif

namespace editor {

namespace fs = std::filesystem;

namespace {

std::string humanSize(std::uintmax_t b) {
    char buf[32];
    if (b < 1024)                   snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)b);
    else if (b < 1024ull * 1024)    snprintf(buf, sizeof(buf), "%.2f KB", (double)b / 1024.0);
    else if (b < 1024ull * 1024 * 1024) snprintf(buf, sizeof(buf), "%.2f MB", (double)b / (1024.0*1024.0));
    else                            snprintf(buf, sizeof(buf), "%.2f GB", (double)b / (1024.0*1024.0*1024.0));
    return buf;
}

std::string humanTime(const fs::file_time_type& ft) {
    // C++17 file_clock → system_clock (best effort cast).
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ft - fs::file_time_type::clock::now()
                       + std::chrono::system_clock::now());
    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    std::tm     tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

void revealInExplorer(const std::string& absPath) {
#ifdef _WIN32
    // /select,"<path>" → opens Explorer and selects the file.
    std::string args = "/select,\"" + absPath + "\"";
    ::ShellExecuteA(nullptr, "open", "explorer.exe",
                    args.c_str(), nullptr, SW_SHOWNORMAL);
#else
    (void)absPath;
#endif
}

// `.mat.json5` extension check — case-insensitive. We use this to switch the
// preview into material-editor mode below.
bool isMaterialAsset(const std::string& path) {
    static const std::string ext = ".mat.json5";
    if (path.size() < ext.size()) return false;
    for (size_t i = 0; i < ext.size(); ++i) {
        char a = (char)tolower((unsigned char)path[path.size() - ext.size() + i]);
        if (a != ext[i]) return false;
    }
    return true;
}

// ---- Material-preview cache --------------------------------------------------
// Loading a material is non-trivial (parse JSON5 + texture-load every channel
// that has a texname). Cache the parsed `material_t` per absolute path; reload
// on mtime change. Saved values write back to disk + bump the cached mtime so
// the next frame doesn't reload pointlessly.
struct MaterialCacheEntry {
    material_t mat{};
    fs::file_time_type mtime{};
    bool loaded = false;
};

MaterialCacheEntry* getOrLoadMaterialCache(const std::string& absPath,
                                           const std::string& projectRoot) {
    static std::unordered_map<std::string, MaterialCacheEntry> cache;

    std::error_code ec;
    fs::file_time_type now = fs::last_write_time(absPath, ec);

    auto it = cache.find(absPath);
    if (it != cache.end()) {
        if (it->second.loaded && it->second.mtime == now) return &it->second;
        // Stale → re-load. The material_t fields are POD; texture pointers
        // leak by design — they're cached engine-side in the texture cache,
        // re-load is idempotent.
        it->second = MaterialCacheEntry{};
    }

    MaterialCacheEntry entry;
    if (material_asset_io::loadMaterial(absPath, projectRoot, &entry.mat)) {
        entry.mtime  = now;
        entry.loaded = true;
    }
    return &(cache[absPath] = std::move(entry));
}

}  // namespace

void drawAssetPreview(EditorApp& app, const std::string& absPath) {
    if (absPath.empty()) return;

    std::error_code ec;
    const bool exists = fs::exists(absPath, ec);

    std::string rel = asset_path::toProjectRelative(absPath, app.projectPath());

    // Header: type label (based on FileTypeRegistry).
    const FileTypeHandler* h = FileTypeRegistry::instance().handlerFor(absPath);
    const std::string typeLabel = h ? h->label : std::string("File");

    ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f),
                       "Asset: %s", typeLabel.c_str());
    ImGui::Separator();
    ImGui::Spacing();

    if (!exists) {
        ImGui::TextColored(ImVec4(1.0f, 0.40f, 0.40f, 1.0f),
                           "(file not found)");
        ImGui::TextDisabled("%s", absPath.c_str());
        if (ImGui::SmallButton("Clear selection")) {
            app.selection().clearSelectedAsset();
        }
        return;
    }

    // ---- File info ---------------------------------------------------------
    auto size  = fs::file_size(absPath, ec);
    auto mtime = fs::last_write_time(absPath, ec);

    ImGui::TextDisabled("Project path:");
    ImGui::TextWrapped("%s", rel.c_str());
    ImGui::Spacing();

    ImGui::TextDisabled("Absolute:");
    ImGui::TextWrapped("%s", absPath.c_str());
    ImGui::Spacing();

    ImGui::Text("Size:     %s", humanSize(size).c_str());
    ImGui::Text("Modified: %s", humanTime(mtime).c_str());
    ImGui::Spacing();

    ImGui::Separator();
    ImGui::Spacing();

    // ---- Action buttons -----------------------------------------------------
    const bool hasSpawn = (h && h->action);
    if (!hasSpawn) ImGui::BeginDisabled();
    if (ImGui::Button("Spawn into scene")) {
        h->action(app, absPath);
    }
    if (!hasSpawn) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Open in IDE")) {
        if (IdeLauncher::instance().openFile(absPath)) {
            app.bus().emit(kEvtLogInfo,
                std::string("[IDE] opened: ") + absPath);
        } else {
            app.bus().emit(kEvtLogWarn,
                std::string("[IDE] no IDE detected"));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reveal in folder")) {
        revealInExplorer(absPath);
    }

    // ---- Material asset editor (Blokk 2.3) ---------------------------------
    // For `.mat.json5` assets, drop the engine's built-in ui_material() right
    // here in the Inspector. The user edits + clicks Save → writes back to disk
    // (mtime-bump suppresses the next-frame reload).
    if (isMaterialAsset(absPath)) {
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "Material");
        ImGui::Spacing();

        MaterialCacheEntry* entry = getOrLoadMaterialCache(
            absPath, app.projectPath());

        if (!entry || !entry->loaded) {
            ImGui::TextColored(ImVec4(1.0f, 0.40f, 0.40f, 1.0f),
                               "Failed to parse material JSON5.");
            ImGui::TextDisabled("Check the file with a text editor.");
        } else {
            if (ImGui::Button("Save Changes")) {
                // Snapshot the file BEFORE the write so we have an undo target.
                std::string before;
                {
                    std::ifstream f(absPath, std::ios::binary);
                    if (f.is_open()) {
                        std::ostringstream ss;
                        ss << f.rdbuf();
                        before = ss.str();
                    }
                }
                if (material_asset_io::writeFile(absPath, entry->mat)) {
                    // Read the new contents for the redo snapshot.
                    std::string after;
                    {
                        std::ifstream f(absPath, std::ios::binary);
                        if (f.is_open()) {
                            std::ostringstream ss;
                            ss << f.rdbuf();
                            after = ss.str();
                        }
                    }
                    // Push an undoable record. Inspector arrows will scrub
                    // this asset's history; global Ctrl+Z also works.
                    if (!before.empty() && !after.empty() && before != after) {
                        app.commands().execute(
                            std::make_unique<AssetStateCommand>(
                                absPath, std::move(before), std::move(after),
                                "Material Save"));
                    }
                    // Bump cached mtime so next frame's mtime-poll doesn't
                    // reload (which would discard mid-edit state).
                    std::error_code ec;
                    entry->mtime = fs::last_write_time(absPath, ec);
                    app.bus().emit(kEvtLogInfo,
                        std::string("[Material] saved: ") + rel);
                } else {
                    app.bus().emit(kEvtLogError,
                        std::string("[Material] write failed: ") + absPath);
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(Ctrl+S also saves the scene, not this file.)");
            ImGui::Separator();

            // Editor's own material UI — per-channel collapsible with
            // texture-input + color-picker + thumbnail integrated in ONE place.
            // (Replaces the engine's ui_material() which had no way to set
            // texname; see material_drawer.h for context.)
            drawMaterial(&entry->mat, app.projectPath());
        }
    }
}

}  // namespace editor
