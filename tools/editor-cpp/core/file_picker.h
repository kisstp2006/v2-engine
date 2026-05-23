#pragma once

#include <string>

namespace editor {

// Win32 natív file picker. Üres string ha Cancel.
// `save=true` → Save As dialog, egyébként Open dialog.
// `extension` pl. "json5" — alapértelmezett kiterjesztés mentésnél.
std::string pickFile(const char* title, const char* extension, bool save);

}  // namespace editor
