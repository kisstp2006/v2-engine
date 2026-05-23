#pragma once

#include <string>
#include <vector>

#include "panel.h"

namespace editor {

// Log-entry severity. draw uses `ImGui::TextColored` based on severity:
// Info=white, Warn=yellow, Error=red.
enum class LogSeverity { Info, Warn, Error };

struct LogEntry {
    std::string  msg;
    LogSeverity  sev;
};

class ConsolePanel : public Panel {
public:
    ConsolePanel() : Panel("console", "Console") {}
    void draw(EditorApp& app) override;

    // Backward-compat: overload WITHOUT severity → Info.
    void log(const std::string& msg) { log(msg, LogSeverity::Info); }
    void log(const std::string& msg, LogSeverity sev);
    void clear() { lines_.clear(); }

private:
    std::vector<LogEntry> lines_;
    bool autoScroll_ = true;
};

}  // namespace editor
