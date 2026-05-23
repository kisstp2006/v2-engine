#pragma once

#include <string>

namespace editor {

// Launch a new editor process with `--project <path>` argument.
// On Windows uses CreateProcessA (not system() / cmd /c), so no
// shell-quoting nonsense. Returns true if the new process started.
bool launchEditorWithProject(const std::string& exe,
                             const std::string& projectPath);

}  // namespace editor
