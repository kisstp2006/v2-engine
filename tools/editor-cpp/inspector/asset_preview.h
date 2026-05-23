#pragma once

// AssetPreview — az Inspector "asset mode"-ja (Phase 3b+ bővítés).
// Ha az `app.selection().selectedAsset()` non-empty és nincs primary node-
// selection, az InspectorPanel ezt rajzolja a node-inspector helyett.
//
// Tartalom:
//   - File-info: path, size, mtime, typing-label (FileTypeRegistry-alapú).
//   - Akciók: "Spawn into scene" (FileTypeHandler::action), "Open in IDE"
//     (IdeLauncher), "Reveal in folder" (ShellExecute "explore").
//
// Egyetlen public függvény, nem class — az Inspector hívja közvetlen.

#include <string>

namespace editor {

class EditorApp;

void drawAssetPreview(EditorApp& app, const std::string& absPath);

}  // namespace editor
