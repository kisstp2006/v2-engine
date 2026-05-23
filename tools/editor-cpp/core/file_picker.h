#pragma once

#include <string>

namespace editor {

// Win32 native file picker. Empty string if Cancel.
// `save=true` → Save As dialog, otherwise Open dialog.
// `extension` e.g. "json5" — default extension when saving.
std::string pickFile(const char* title, const char* extension, bool save);

}  // namespace editor
