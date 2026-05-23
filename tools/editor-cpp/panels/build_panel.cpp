// STL FIRST.
#include <any>
#include <string>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "build_panel.h"
#include "../app/editor_app.h"
#include "../app/panel_registry.h"
#include "../core/event_bus.h"
#include "../core/events.h"

namespace editor {

namespace {

constexpr size_t kLogTailMax = 200;

void pushLog(std::vector<std::string>& tail, std::string line) {
    tail.push_back(std::move(line));
    if (tail.size() > kLogTailMax) {
        tail.erase(tail.begin(),
                   tail.begin() + (tail.size() - kLogTailMax));
    }
}

}  // namespace

void BuildPanel::wireUpIfNeeded(EditorApp& app) {
    if (wired_) return;
    wired_ = true;

    app.bus().on(kEvtCookStarted, [this](const std::any& d) {
        if (auto* p = std::any_cast<CookProgress>(&d)) {
            cur_   = p->current;
            total_ = p->total;
            currentFile_.clear();
            lastResult_.clear();
            logTail_.clear();
            cookActive_ = true;
            visible = true;        // auto-show on start
            pushLog(logTail_,
                std::string("[started] ") + std::to_string(p->total) +
                " files");
        }
    });

    app.bus().on(kEvtCookProgress, [this](const std::any& d) {
        if (auto* p = std::any_cast<CookProgress>(&d)) {
            cur_         = p->current;
            total_       = p->total;
            currentFile_ = p->currentFile;
            pushLog(logTail_,
                "[" + std::to_string(p->current) + "/" +
                std::to_string(p->total) + "] " + p->currentFile);
        }
    });

    app.bus().on(kEvtCookFinished, [this](const std::any& d) {
        cookActive_ = false;
        currentFile_.clear();
        if (auto* r = std::any_cast<CookResult>(&d)) {
            std::string msg = "Done: " + std::to_string(r->succeeded) +
                              " ok, " + std::to_string(r->failed) + " fail";
            if (!r->outputPath.empty()) msg += " → " + r->outputPath;
            lastResult_ = msg;
            pushLog(logTail_, "[finished] " + msg);
        } else {
            lastResult_ = "Done.";
        }
    });

    app.bus().on(kEvtCookCancelled, [this](const std::any& d) {
        cookActive_ = false;
        currentFile_.clear();
        if (auto* r = std::any_cast<CookResult>(&d)) {
            int done = r->succeeded + r->failed;
            std::string msg = "Cancelled: " + std::to_string(r->succeeded) +
                              " ok, " + std::to_string(r->failed) + " fail (" +
                              std::to_string(done) + "/" +
                              std::to_string(r->total) + ")";
            lastResult_ = msg;
            pushLog(logTail_, "[cancelled] " + msg);
        } else {
            lastResult_ = "Cancelled.";
        }
    });
}

void BuildPanel::draw(EditorApp& app) {
    wireUpIfNeeded(app);
    if (!visible) return;

    if (!ImGui::Begin(title_.c_str(), &visible)) {
        ImGui::End();
        return;
    }

    // ---- Header --------------------------------------------------------
    if (cookActive_) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.30f, 1.0f),
                           "Cooking: %d / %d", cur_, total_);
    } else if (!lastResult_.empty()) {
        ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.55f, 1.0f),
                           "%s", lastResult_.c_str());
    } else {
        ImGui::TextDisabled("Idle. Use Tools menu to start a cook.");
    }

    // ---- Progress bar --------------------------------------------------
    float frac = (total_ > 0) ? (float)cur_ / (float)total_ : 0.0f;
    char overlay[64];
    snprintf(overlay, sizeof(overlay), "%d / %d", cur_, total_);
    ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0), overlay);

    // ---- Current file --------------------------------------------------
    if (!currentFile_.empty()) {
        ImGui::TextDisabled("Current: %s", currentFile_.c_str());
    } else if (cookActive_) {
        ImGui::TextDisabled("Current: (preparing...)");
    } else {
        ImGui::TextDisabled("Current: -");
    }

    // ---- Cancel button (only during cook) ------------------------------
    if (cookActive_) {
        if (ImGui::Button("Cancel Cook")) {
            app.requestCookCancel();
        }
    } else {
        ImGui::BeginDisabled();
        ImGui::Button("Cancel Cook");
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear Log")) {
        logTail_.clear();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Log (%zu lines):", logTail_.size());

    // ---- Log-tail ScrolledChild ---------------------------------------
    if (ImGui::BeginChild("##build_log", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const auto& line : logTail_) {
            ImGui::TextUnformatted(line.c_str());
        }
        if (cookActive_ &&
            ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

REGISTER_PANEL(BuildPanel, 550)

}  // namespace editor
