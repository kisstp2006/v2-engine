#pragma once

#include <string>

namespace editor {

// Új editor process indítása `--project <path>` argumentummal.
// Windows-on CreateProcessA-t használ (nem system() / cmd /c), így nincs
// shell-quoting hülyeség. Visszaad true-t ha az új process elindult.
bool launchEditorWithProject(const std::string& exe,
                             const std::string& projectPath);

}  // namespace editor
