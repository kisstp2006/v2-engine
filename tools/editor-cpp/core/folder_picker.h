#pragma once

#include <string>

namespace editor {

// Modal native folder picker. Returns empty string if Cancel.
// Windows: IFileOpenDialog + FOS_PICKFOLDERS.
std::string pickFolder(const char* title);

}  // namespace editor
