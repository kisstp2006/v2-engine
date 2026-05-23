#pragma once

// AssetPreview — the Inspector's "asset mode" (Phase 3b+ extension).
// If `app.selection().selectedAsset()` is non-empty and there's no primary
// node selection, the InspectorPanel draws this instead of the node-inspector.
//
// Contents:
//   - File-info: path, size, mtime, typing-label (FileTypeRegistry-based).
//   - Actions: "Spawn into scene" (FileTypeHandler::action), "Open in IDE"
//     (IdeLauncher), "Reveal in folder" (ShellExecute "explore").
//
// A single public function, not a class — the Inspector calls it directly.

#include <string>

namespace editor {

class EditorApp;

void drawAssetPreview(EditorApp& app, const std::string& absPath);

}  // namespace editor
