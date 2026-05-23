#pragma once

#include <string>

namespace editor {

// Modális natív mappa-választó. Üres string-et ad vissza ha Cancel.
// Windows: IFileOpenDialog + FOS_PICKFOLDERS.
std::string pickFolder(const char* title);

}  // namespace editor
