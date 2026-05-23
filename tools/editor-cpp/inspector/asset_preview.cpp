// STL + ide_helper FIRST (the engine's `ifdef_*` macros corrupt
// `<filesystem>` in the stdlib headers — pure-STL must come first).
#include <chrono>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#include "../core/ide_launcher.h"

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "asset_preview.h"
#include "../app/editor_app.h"
#include "../app/file_type_registry.h"
#include "../core/asset_path.h"
#include "../core/event_bus.h"
#include "../core/events.h"

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
}

}  // namespace editor
