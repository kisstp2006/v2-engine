// STL FIRST.
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "engine.h"
#include <cimguizmo/cimguizmo.h>

#include "editor_app.h"
#include "panel_registry.h"

#include "../core/folder_picker.h"
#include "../core/file_picker.h"
#include "../core/events.h"
#include "../core/process_launcher.h"
#include "../core/asset_path.h"
#include "../core/fx_bundle.h"
#include "../panels/panel.h"
#include "../panels/console_panel.h"
#include "../components/components_api.h"
#include "../persistence/scene_io.h"
#include "../persistence/postfx_state_io.h"
#include "../runtime/script_host.h"
#include "../runtime/cook_runner.h"
#include "../runtime/asset_validator.h"
#include "../runtime/ffi_to_emmylua.h"
#include "../scene/scene_helpers.h"

namespace editor {

EditorApp::EditorApp(std::string projectPath)
    : projectPath_(std::move(projectPath)) {
    buildPanels();
    wireUp();
    // PostFX shaders: load every `<project>/assets/fx/*.glsl` once at startup
    // so the engine's `fx_*` API has the passes available the moment a
    // PostFXStack node turns on. No-op if the project has no assets/fx/.
    loadProjectFXShaders();
}

EditorApp::~EditorApp() = default;

void EditorApp::buildPanels() {
    // Built from the registry — every panel registers itself via
    // REGISTER_PANEL(Type, order) at the end of its own .cpp.
    for (const auto& d : PanelRegistry::all()) {
        panels_.emplace_back(d.factory());
    }
    // Pointer-cache the Console for the bus dispatcher.
    for (auto& p : panels_) {
        if (p->id() == "console") {
            console_ = static_cast<ConsolePanel*>(p.get());
            break;
        }
    }
}

std::string EditorApp::makeRelativeIfInside(const char* inputPath) {
    if (!inputPath || !*inputPath) return {};
    // If already relative, leave it alone.
    if (!asset_path::isAbsolute(inputPath)) return inputPath;
    // Abs + outside project → keep as-is + warning.
    if (!asset_path::isWithinProject(inputPath, projectPath_)) {
        bus_.emit(kEvtLogWarn,
            std::string("[Asset] outside project, abs-path kept: ") + inputPath);
        return inputPath;
    }
    return asset_path::toProjectRelative(inputPath, projectPath_);
}

obj* EditorApp::ensureRoot() {
    if (scene_.root()) return scene_.root();
    obj* defaultRoot = editor_obj_new_scene("Scene");
    scene_.replaceRoot(defaultRoot);   // emit kEvtSceneReplaced
    if (console_) {
        console_->log("[Scene] no root was loaded, created default 'Scene'");
    }
    return defaultRoot;
}

obj* EditorApp::createEmpty(obj* parent) {
    obj* p = parent ? parent : ensureRoot();
    if (!p) return nullptr;
    obj* n = editor_obj_new_transform(p, "GameObject");
    selection_.setPrimary(n);
    commands_.execute(std::make_unique<AddNodeCommand>(p, n, "Add Empty"));
    if (console_) {
        console_->log(std::string("Created: ") +
                      (obj_name(n) ? obj_name(n) : "GameObject"));
    }
    return n;
}

obj* EditorApp::createMesh(const char* model_path, obj* parent) {
    obj* p = parent ? parent : ensureRoot();
    if (!p) return nullptr;
    std::string rel = makeRelativeIfInside(model_path);
    obj* n = editor_obj_new_mesh_renderer(p, "Mesh", rel.c_str());
    selection_.setPrimary(n);
    commands_.execute(std::make_unique<AddNodeCommand>(p, n, "Add Mesh"));
    if (console_) {
        std::string msg = "Created: Mesh";
        if (!rel.empty()) { msg += " ("; msg += rel; msg += ")"; }
        console_->log(msg);
    }
    return n;
}

obj* EditorApp::createSprite(const char* texture_path, obj* parent) {
    obj* p = parent ? parent : ensureRoot();
    if (!p) return nullptr;
    std::string rel = makeRelativeIfInside(texture_path);
    obj* n = editor_obj_new_sprite_renderer(p, "Sprite", rel.c_str());
    selection_.setPrimary(n);
    commands_.execute(std::make_unique<AddNodeCommand>(p, n, "Add Sprite"));
    if (console_) {
        std::string msg = "Created: Sprite";
        if (!rel.empty()) { msg += " ("; msg += rel; msg += ")"; }
        console_->log(msg);
    }
    return n;
}

namespace {

// DFS for the first PostFXStack node in the scene (NULL if none).
obj* findPostFXStackNode_save(obj* node) {
    if (!node) return nullptr;
    if (editor_obj_is_postfx_stack(node)) return node;
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        obj* r = findPostFXStackNode_save(editor_obj_child_at(node, i));
        if (r) return r;
    }
    return nullptr;
}

// PostFX state lives in a sidecar JSON5 file next to the scene. Reason:
// the v2-engine reflection-IO's char* field has a hard 128-byte truncation
// + first-newline cutoff (obj_obj.h:864-871) that can't hold a multi-pass
// blob. Sidecar bypasses the limit and keeps the scene file readable.
//
// Naming convention: `<scene>.json5` → `<scene>.postfx.json5`. The `.json5`
// suffix is stripped first so the sidecar is also a valid JSON5 (mirrors the
// `.prefab.json5` family).
std::string postfxSidecarPath(const std::string& scenePath) {
    static const std::string kJson5 = ".json5";
    if (scenePath.size() > kJson5.size() &&
        scenePath.compare(scenePath.size() - kJson5.size(),
                          kJson5.size(), kJson5) == 0) {
        return scenePath.substr(0, scenePath.size() - kJson5.size()) +
               ".postfx.json5";
    }
    return scenePath + ".postfx.json5";
}

// Pre-save hook: snapshot the live engine state into the sidecar file.
// No-op if the scene has no PostFXStack node (we don't litter sidecars
// next to PostFX-free scenes).
void writePostFXSidecar(const std::string& scenePath, obj* root,
                        ConsolePanel* console) {
    if (!findPostFXStackNode_save(root)) return;
    std::string sidecar = postfxSidecarPath(scenePath);
    std::string blob    = postfx_state_io::snapshotEngineState();
    int ok = file_write(sidecar.c_str(), blob.c_str(), (int)blob.size());
    if (console) {
        console->log(std::string(ok ? "[PostFX] sidecar saved: "
                                    : "[PostFX] sidecar save failed: ")
                     + sidecar);
    }
}

// Post-load hook: read the sidecar (if present) and apply onto the engine.
// No-op if the scene has no PostFXStack node — the engine fx_* state still
// exists, but is rendering-irrelevant without a PostFXStack triggering
// fx_begin/end in the render-walk.
void readPostFXSidecar(const std::string& scenePath, obj* root,
                       ConsolePanel* console) {
    if (!findPostFXStackNode_save(root)) return;
    std::string sidecar = postfxSidecarPath(scenePath);
    if (!is_file(sidecar.c_str())) return;

    int sz = 0;
    char* content = file_read(sidecar.c_str(), &sz);
    if (!content || sz <= 0) return;

    auto ar = postfx_state_io::applyEngineState(std::string(content, sz));
    if (console) {
        char buf[160];
        snprintf(buf, sizeof(buf),
            "[PostFX] applied: %d/%d passes, %d uniforms (%d skipped)",
            ar.passes_applied,
            ar.passes_applied + ar.passes_missing,
            ar.uniforms_applied, ar.uniforms_skipped);
        console->log(buf);
        for (const auto& w : ar.warnings) {
            console->log(std::string("[PostFX] ") + w);
        }
    }
}

}  // namespace

void EditorApp::saveScene() {
    if (lastSavedPath_.empty()) { saveSceneAs(); return; }
    std::string json = SceneIO::saveTree(scene_.root());
    int ok = file_write(lastSavedPath_.c_str(), json.c_str(), (int)json.size());
    if (ok) {
        isDirty_ = false;
        bus_.emit(kEvtSceneDirty, false);
        // Sidecar AFTER the scene file succeeds — keeps the pair in sync.
        writePostFXSidecar(lastSavedPath_, scene_.root(), console_);
    }
    if (console_) {
        console_->log(std::string(ok ? "[Scene] saved: " : "[Scene] save failed: ")
                      + lastSavedPath_);
    }
}

void EditorApp::saveSceneAs() {
    std::string path = pickFile("Save Scene", "json5", true);
    if (path.empty()) return;

    std::string json = SceneIO::saveTree(scene_.root());
    int ok = file_write(path.c_str(), json.c_str(), (int)json.size());
    if (ok) {
        lastSavedPath_ = path;
        isDirty_ = false;
        bus_.emit(kEvtSceneDirty, false);
        writePostFXSidecar(path, scene_.root(), console_);
    }
    if (console_) {
        console_->log(std::string(ok ? "[Scene] saved: " : "[Scene] save failed: ")
                      + path);
    }
}

extern "C" {
extern char apptitle[128];   // engine `game_app2.h:60` global buffer
}

void EditorApp::refreshWindowTitle() {
    const char* sceneName = "untitled";
    if (!lastSavedPath_.empty()) {
        // basename
        size_t s1 = lastSavedPath_.find_last_of('/');
        size_t s2 = lastSavedPath_.find_last_of('\\');
        size_t s  = (s1 == std::string::npos) ? s2
                   : (s2 == std::string::npos) ? s1
                   : (s1 > s2 ? s1 : s2);
        sceneName = (s == std::string::npos)
                    ? lastSavedPath_.c_str()
                    : lastSavedPath_.c_str() + s + 1;
    }
    snprintf(apptitle, sizeof(apptitle), "%s%s - editor-cpp [%s]",
             sceneName, isDirty_ ? "*" : "",
             projectPath_.empty() ? "no project" : projectPath_.c_str());
}

void EditorApp::openScene() {
    std::string path = pickFile("Open Scene", "json5", false);
    if (path.empty()) return;
    openScene(path);
}

void EditorApp::openScene(const std::string& path) {
    int size = 0;
    char* content = file_read(path.c_str(), &size);
    if (!content) {
        if (console_) console_->log(std::string("[Scene] read failed: ") + path);
        return;
    }
    // Phase 4b — auto-migration: convert old abs-path fields in scenes
    // to relative during load.
    LoadResult r = SceneIO::loadTreeDetailed(std::string(content), projectPath_);
    if (console_) {
        for (const auto& e : r.errors) {
            console_->log(std::string("[Scene] ") + e);
        }
        console_->log("[Scene] open: " + std::to_string(r.created) + " created, "
                      + std::to_string(r.failed) + " failed → " + path);
        if (r.migrated_paths > 0) {
            console_->log("[Scene] migrated " + std::to_string(r.migrated_paths) +
                          " abs-paths to project-relative — Save to persist");
        }
    }
    if (!r.root) return;
    scene_.replaceRoot(r.root);   // kEvtSceneReplaced → selection sanitize
    lastSavedPath_ = path;
    // If path-migration happened → dirty (user sees the *, and can Save).
    isDirty_ = (r.migrated_paths > 0);
    bus_.emit(kEvtSceneDirty, isDirty_);
    // Pick up any new FX shaders the user dropped into assets/fx/ since
    // the ctor (e.g. via Tools → Import, or copied in by hand). dedup-set
    // means already-loaded ones are skipped.
    loadProjectFXShaders();
    // Re-apply the snapshotted PostFX state (per-pass enabled / priority /
    // uniform-values) from the `.postfx.json5` sidecar. Order matters: must
    // run AFTER loadProjectFXShaders so fx_find() can resolve the names.
    readPostFXSidecar(path, scene_.root(), console_);
}

obj* EditorApp::createTilemap(const char* tmx_path, obj* parent) {
    obj* p = parent ? parent : ensureRoot();
    if (!p) return nullptr;
    std::string rel = makeRelativeIfInside(tmx_path);
    obj* n = editor_obj_new_tilemap_ref(p, "Tilemap", rel.c_str());
    selection_.setPrimary(n);
    commands_.execute(std::make_unique<AddNodeCommand>(p, n, "Add Tilemap"));
    if (console_) {
        std::string msg = "Created: Tilemap";
        if (!rel.empty()) { msg += " ("; msg += rel; msg += ")"; }
        console_->log(msg);
    }
    return n;
}

obj* EditorApp::createLight(int type, obj* parent) {
    obj* p = parent ? parent : ensureRoot();
    if (!p) return nullptr;
    const char* tname = (type == 0) ? "Directional Light"
                      : (type == 1) ? "Point Light"
                      : (type == 2) ? "Spot Light"
                      : "Light";
    obj* n = editor_obj_new_light_ref(p, tname, type);
    selection_.setPrimary(n);
    commands_.execute(std::make_unique<AddNodeCommand>(p, n, "Add Light"));
    if (console_) console_->log(std::string("Created: ") + tname);
    return n;
}

obj* EditorApp::createCamera(obj* parent) {
    obj* p = parent ? parent : ensureRoot();
    if (!p) return nullptr;
    obj* n = editor_obj_new_camera_ref(p, "Camera");
    selection_.setPrimary(n);
    commands_.execute(std::make_unique<AddNodeCommand>(p, n, "Add Camera"));
    if (console_) console_->log("Created: Camera");
    return n;
}

obj* EditorApp::createFogSettings(obj* parent) {
    obj* p = parent ? parent : ensureRoot();
    if (!p) return nullptr;
    obj* n = editor_obj_new_fog_settings(p, "Fog");
    selection_.setPrimary(n);
    commands_.execute(std::make_unique<AddNodeCommand>(p, n, "Add Fog"));
    if (console_) console_->log("Created: Fog");
    return n;
}

obj* EditorApp::createSkybox(const char* sky_path, obj* parent) {
    obj* p = parent ? parent : ensureRoot();
    if (!p) return nullptr;
    std::string rel = (sky_path && *sky_path)
                    ? makeRelativeIfInside(sky_path)
                    : std::string();
    obj* n = editor_obj_new_skybox(p, "Skybox", rel.c_str());
    selection_.setPrimary(n);
    commands_.execute(std::make_unique<AddNodeCommand>(p, n, "Add Skybox"));
    if (console_) console_->log("Created: Skybox");
    return n;
}

obj* EditorApp::createPostFXStack(obj* parent) {
    obj* p = parent ? parent : ensureRoot();
    if (!p) return nullptr;
    obj* n = editor_obj_new_postfx_stack(p, "PostFX Stack");
    selection_.setPrimary(n);
    commands_.execute(std::make_unique<AddNodeCommand>(p, n, "Add PostFX Stack"));
    if (console_) console_->log("Created: PostFX Stack");
    return n;
}

obj* EditorApp::createTextRenderer(const char* text, obj* parent) {
    obj* p = parent ? parent : ensureRoot();
    if (!p) return nullptr;
    obj* n = editor_obj_new_text_renderer(p, "Text", text);
    selection_.setPrimary(n);
    commands_.execute(std::make_unique<AddNodeCommand>(p, n, "Add Text"));
    if (console_) console_->log("Created: Text");
    return n;
}

obj* EditorApp::createText3DRenderer(const char* text, obj* parent) {
    obj* p = parent ? parent : ensureRoot();
    if (!p) return nullptr;
    obj* n = editor_obj_new_text_renderer_3d(p, "Text3D", text);
    selection_.setPrimary(n);
    commands_.execute(std::make_unique<AddNodeCommand>(p, n, "Add Text3D"));
    if (console_) console_->log("Created: Text3D");
    return n;
}

obj* EditorApp::createAudioSource(const char* clip_path, obj* parent) {
    obj* p = parent ? parent : ensureRoot();
    if (!p) return nullptr;
    std::string rel = makeRelativeIfInside(clip_path);
    obj* n = editor_obj_new_audio_source(p, "AudioSource", rel.c_str());
    selection_.setPrimary(n);
    commands_.execute(std::make_unique<AddNodeCommand>(p, n, "Add AudioSource"));
    if (console_) {
        std::string msg = "Created: AudioSource";
        if (!rel.empty()) { msg += " ("; msg += rel; msg += ")"; }
        console_->log(msg);
    }
    return n;
}

void EditorApp::saveSelectedAsPrefab() {
    obj* node = selection_.primary();
    if (!node) {
        if (console_) console_->log("[Prefab] no selection");
        return;
    }
    std::string path = pickFile("Save Prefab", "prefab.json5", true);
    if (path.empty()) return;
    std::string json = SceneIO::saveSubtree(node);
    int ok = file_write(path.c_str(), json.c_str(), (int)json.size());
    if (console_) {
        console_->log(std::string(ok ? "[Prefab] saved: " : "[Prefab] save failed: ")
                      + path);
    }
}

ScriptHost& EditorApp::scriptHost() {
    if (!scriptHost_) scriptHost_ = std::make_unique<ScriptHost>(*this);
    return *scriptHost_;
}

CookRunner& EditorApp::cookRunner() {
    if (!cookRunner_)
        cookRunner_ = std::make_unique<CookRunner>(*this, mainQueue_);
    return *cookRunner_;
}

namespace {

// Phase 5a — abs-path list of every file under assets/ (hidden-prefix,
// already-cooked files skipped).
std::vector<std::string> collectAssetFiles(const std::string& projectPath) {
    std::vector<std::string> out;
    if (projectPath.empty()) return out;
    namespace fs = std::filesystem;
    fs::path assets = fs::path(projectPath) / "assets";
    std::error_code ec;
    if (!fs::is_directory(assets, ec)) return out;

    for (auto it = fs::recursive_directory_iterator(assets, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        std::string name = it->path().filename().string();
        if (!name.empty() && name[0] == '.') continue;  // skip cooked cache
        out.push_back(it->path().string());
    }
    return out;
}

}  // namespace

namespace {

// Phase 5c — distribute Console-log from issue-list. By severity:
// kEvtLogInfo/Warn/Error → colored in Console.
void logIssues(EditorApp& app, const std::vector<AssetIssue>& issues) {
    for (const auto& i : issues) {
        std::string line = std::string("[Validation] ") + i.typeName + " '" +
                           i.nodeName + "' " + i.fieldName + " = \"" +
                           i.path + "\" → " + i.reason;
        const char* key = (i.level == AssetIssueLevel::Error) ? kEvtLogError
                        : (i.level == AssetIssueLevel::Warning) ? kEvtLogWarn
                                                                : kEvtLogInfo;
        app.bus().emit(key, line);
    }
}

}  // namespace

void EditorApp::generateLuaStubs(bool force) {
    if (projectPath_.empty()) {
        bus_.emit(kEvtLogWarn, std::string("[LuaIDE] no project loaded"));
        return;
    }
    // The engine's `engine.ffi` lives in the v2 repo `code/game/embed/` —
    // relative to editor-cpp.exe CWD this is "code/game/embed/engine.ffi".
    // (ScriptHost reads it the same way: script_host.cpp:36.)
    std::string ffiPath = "code/game/embed/engine.ffi";
    auto r = ffi_to_emmylua::generate(projectPath_, ffiPath, force);
    if (!r.ok) {
        bus_.emit(kEvtLogError, std::string("[LuaIDE] ") + r.error);
        return;
    }
    char buf[256];
    snprintf(buf, sizeof(buf),
        "[LuaIDE] generated: %d functions, %d classes → %s",
        r.functions, r.classes, r.stubPath.c_str());
    bus_.emit(kEvtLogInfo, std::string(buf));
    bus_.emit(kEvtLogInfo, std::string("[LuaIDE] config: ") + r.configPath);
    if (!r.vscodePath.empty()) {
        bus_.emit(kEvtLogInfo, std::string("[LuaIDE] vscode: ") + r.vscodePath);
    }
}

void EditorApp::importDefaultFXShaders() {
    if (projectPath_.empty()) {
        bus_.emit(kEvtLogWarn, std::string("[FX] no project loaded"));
        return;
    }
    // Skip-existing mode — never clobber user-edited variants. If the
    // user wants a fresh copy they can delete the file first.
    auto r = fx_bundle::copyBundledShaders(projectPath_, /*overwrite=*/false);
    if (r.source_dir_missing) {
        bus_.emit(kEvtLogError,
            std::string("[FX] bundled shader dir not found "
                        "(tools/editor-cpp/embed/fx/) — is the editor "
                        "launched from the v2 repo root?"));
        return;
    }
    char buf[160];
    snprintf(buf, sizeof(buf),
        "[FX] imported %d / %d  (skipped %d existing)",
        r.copied, r.total, r.skipped);
    bus_.emit(kEvtLogInfo, std::string(buf));
    // Newly-imported shaders are picked up on the next loadProjectFXShaders().
    loadProjectFXShaders();
}

void EditorApp::loadProjectFXShaders() {
    if (projectPath_.empty()) return;
    namespace fs = std::filesystem;
    fs::path fxDir = fs::path(projectPath_) / "assets" / "fx";
    std::error_code ec;
    if (!fs::is_directory(fxDir, ec)) return;

    // The motor's `fx_load(glob)` uses CWD-relative `file_list` (see
    // code/sys/sys_file.h:282), so an abs-path glob produces no matches.
    // Work around it: list with std::filesystem, slurp with `file_read`,
    // feed each new shader to `fx_load_from_mem`. The motor-side
    // `postfx_load_from_mem` does NOT dedupe, so we track loaded names
    // ourselves to be safe on re-entry (Tools → Import, openScene, etc.).
    int loaded = 0;
    for (auto& e : fs::directory_iterator(fxDir, ec)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != ".glsl") continue;
        std::string name = e.path().filename().string();
        if (loadedFXNames_.count(name)) continue;

        std::string abs = e.path().string();
        int sz = 0;
        char* content = file_read(abs.c_str(), &sz);
        if (!content || sz <= 0) continue;

        // `fx_load_from_mem` STRDUPs the nameid for `passfx.name`, so a
        // stack-local c_str() would be fine — but mirror the engine's
        // own `fx_load` convention (which keeps a heap STRDUP in the
        // dedupe-set) so the nameid stays valid for later set-lookup
        // inside the motor.
        char* nameHeap = STRDUP(name.c_str());
        int slot = fx_load_from_mem(nameHeap, content);
        if (slot >= 0) {
            loadedFXNames_.insert(std::move(name));
            ++loaded;
        }
    }
    if (loaded > 0) {
        char buf[160];
        snprintf(buf, sizeof(buf),
            "[FX] loaded %d new shader%s from %s",
            loaded, loaded == 1 ? "" : "s", fxDir.string().c_str());
        bus_.emit(kEvtLogInfo, std::string(buf));
    }
}

void EditorApp::runAssetValidation() {
    auto issues = AssetValidator::validate(*this);
    int errs = AssetValidator::countErrors(issues);
    int wrns = AssetValidator::countWarnings(issues);
    logIssues(*this, issues);
    std::string summary = std::string("[Validation] ") +
        std::to_string(errs) + " errors, " +
        std::to_string(wrns) + " warnings (" +
        std::to_string(issues.size()) + " total)";
    bus_.emit(errs > 0 ? kEvtLogError : kEvtLogInfo, summary);
}

void EditorApp::startCookInPlaceAllAssets() {
    if (projectPath_.empty()) {
        bus_.emit(kEvtLogWarn, std::string("[Cook] no project loaded"));
        return;
    }
    std::vector<std::string> paths = collectAssetFiles(projectPath_);
    if (paths.empty()) {
        bus_.emit(kEvtLogWarn,
            std::string("[Cook] no assets to cook under: ") + projectPath_);
        return;
    }
    // Phase 5c — pre-cook validation. On Error: modal prompt, cook defer.
    auto issues = AssetValidator::validate(*this);
    logIssues(*this, issues);
    int errs = AssetValidator::countErrors(issues);
    if (errs > 0) {
        cookPrompt_.kind          = PendingCookKind::InPlace;
        cookPrompt_.paths         = std::move(paths);
        cookPrompt_.zipPath.clear();
        cookPrompt_.issues        = std::move(issues);
        cookPrompt_.openRequested = true;
        return;
    }
    if (!cookRunner().startCookInPlace(std::move(paths))) {
        bus_.emit(kEvtLogWarn,
            std::string("[Cook] a cook job is already running"));
    }
}

void EditorApp::startBuildCookZip() {
    if (projectPath_.empty()) {
        bus_.emit(kEvtLogWarn, std::string("[Cook] no project loaded"));
        return;
    }
    std::string zip = pickFile("Build cook.zip", "zip", true);
    if (zip.empty()) return;
    std::vector<std::string> paths = collectAssetFiles(projectPath_);
    if (paths.empty()) {
        bus_.emit(kEvtLogWarn,
            std::string("[Cook] no assets to cook under: ") + projectPath_);
        return;
    }
    auto issues = AssetValidator::validate(*this);
    logIssues(*this, issues);
    int errs = AssetValidator::countErrors(issues);
    if (errs > 0) {
        cookPrompt_.kind          = PendingCookKind::BuildZip;
        cookPrompt_.paths         = std::move(paths);
        cookPrompt_.zipPath       = std::move(zip);
        cookPrompt_.issues        = std::move(issues);
        cookPrompt_.openRequested = true;
        return;
    }
    if (!cookRunner().startBuildZip(std::move(paths), std::move(zip))) {
        bus_.emit(kEvtLogWarn,
            std::string("[Cook] a cook job is already running"));
    }
}

void EditorApp::requestCookCancel() {
    if (cookRunner_) cookRunner_->requestCancel();
}

void EditorApp::drawCookValidationPopup() {
    // One-shot OpenPopup trigger.
    if (cookPrompt_.openRequested) {
        ImGui::OpenPopup("Cook validation issues");
        cookPrompt_.openRequested = false;
    }

    // The modal always tries to render — if NOT open, BeginPopupModal
    // returns false immediately and the content does not run.
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(420, 200), ImVec2(900, 600));

    if (ImGui::BeginPopupModal("Cook validation issues", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        int errs = AssetValidator::countErrors(cookPrompt_.issues);
        int wrns = AssetValidator::countWarnings(cookPrompt_.issues);
        ImGui::TextColored(ImVec4(1.0f, 0.40f, 0.40f, 1.0f),
            "Found %d errors, %d warnings.", errs, wrns);
        ImGui::TextDisabled("Cooking may produce broken or missing assets.");
        ImGui::Separator();

        if (ImGui::BeginChild("##cook_issues",
                              ImVec2(600, 240), true,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            for (const auto& i : cookPrompt_.issues) {
                ImVec4 col = (i.level == AssetIssueLevel::Error)
                    ? ImVec4(1.0f, 0.40f, 0.40f, 1.0f)
                    : (i.level == AssetIssueLevel::Warning)
                        ? ImVec4(1.0f, 0.85f, 0.30f, 1.0f)
                        : ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
                const char* lvl = (i.level == AssetIssueLevel::Error) ? "ERR"
                                 : (i.level == AssetIssueLevel::Warning) ? "WRN"
                                                                         : "INF";
                ImGui::TextColored(col, "[%s] %s '%s' %s = \"%s\"",
                    lvl, i.typeName.c_str(), i.nodeName.c_str(),
                    i.fieldName.c_str(), i.path.c_str());
                ImGui::TextDisabled("      %s", i.reason.c_str());
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("Continue Cook Anyway")) {
            switch (cookPrompt_.kind) {
            case PendingCookKind::InPlace:
                cookRunner().startCookInPlace(std::move(cookPrompt_.paths));
                break;
            case PendingCookKind::BuildZip:
                cookRunner().startBuildZip(std::move(cookPrompt_.paths),
                                           std::move(cookPrompt_.zipPath));
                break;
            default: break;
            }
            cookPrompt_.kind = PendingCookKind::None;
            cookPrompt_.issues.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            cookPrompt_.kind = PendingCookKind::None;
            cookPrompt_.paths.clear();
            cookPrompt_.zipPath.clear();
            cookPrompt_.issues.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

obj* EditorApp::createScript(const char* script_path, obj* parent) {
    obj* p = parent ? parent : ensureRoot();
    if (!p) return nullptr;
    std::string rel = makeRelativeIfInside(script_path);
    obj* n = editor_obj_new_script(p, "Script", rel.c_str());
    selection_.setPrimary(n);
    commands_.execute(std::make_unique<AddNodeCommand>(p, n, "Add Script"));
    if (console_) {
        std::string msg = "Created: Script";
        if (!rel.empty()) { msg += " ("; msg += rel; msg += ")"; }
        console_->log(msg);
    }
    return n;
}

void EditorApp::spawnPrefab(const char* path) {
    if (!path || !*path) return;
    int size = 0;
    char* content = file_read(path, &size);
    if (!content) {
        if (console_) console_->log(std::string("[Prefab] read failed: ") + path);
        return;
    }
    obj* root = scene_.root() ? scene_.root() : ensureRoot();
    // Phase 4b — projectPath_ passed in: abs-paths inside the prefab also become rel.
    obj* sub = SceneIO::loadSubtree(root, std::string(content), projectPath_);
    if (!sub) {
        if (console_) console_->log(std::string("[Prefab] parse failed: ") + path);
        return;
    }
    selection_.setPrimary(sub);
    commands_.execute(std::make_unique<AddNodeCommand>(root, sub, "Spawn Prefab"));
    if (console_) console_->log(std::string("[Prefab] spawned: ") + path);
}

void EditorApp::wireUp() {
    // SceneService → bus, so that replaceRoot emits kEvtSceneReplaced.
    scene_.setBus(&bus_);
    commands_.setBus(&bus_);

    // Phase 3e — severity-aware log dispatcher (3x the same code, only different sev).
    auto subscribeLog = [this](const char* key, LogSeverity sev) {
        bus_.on(key, [this, sev](const std::any& data) {
            if (!console_) return;
            if (data.type() == typeid(std::string)) {
                console_->log(std::any_cast<const std::string&>(data), sev);
            } else if (data.type() == typeid(const char*)) {
                console_->log(std::any_cast<const char*>(data), sev);
            }
        });
    };
    subscribeLog(kEvtLogInfo,  LogSeverity::Info);
    subscribeLog(kEvtLogWarn,  LogSeverity::Warn);
    subscribeLog(kEvtLogError, LogSeverity::Error);
    bus_.on(kEvtSelectionChanged, [this](const std::any& data) {
        // New payload: SelectionChange struct. Backward-compat: if anyone still
        // sends a raw obj*, we accept that too.
        obj* o = nullptr;
        if (data.type() == typeid(SelectionChange)) {
            const auto& sc = std::any_cast<const SelectionChange&>(data);
            o = sc.primary;
        } else {
            obj* const* op = std::any_cast<obj*>(&data);
            o = op ? *op : nullptr;
        }
        const char* name = o ? obj_name(o) : "(none)";
        if (console_) {
            console_->log(std::string("Selection: ") +
                          (name ? name : "(unnamed)"));
        }
    });
    // Scene-dirty → flag-set + title-refresh.
    bus_.on(kEvtSceneDirty, [this](const std::any& data) {
        bool dirty = true;
        if (data.type() == typeid(bool)) dirty = std::any_cast<bool>(data);
        isDirty_ = dirty;
    });

    // Scene replace → defensive cleanup (ORDER MATTERS):
    //   1. ScriptHost: lua_close on every VM (obj-pointer-key dangling).
    //   2. PlayMode audio: no previous AudioSource node exists in the new scene.
    //   3. SelectionService: drop the old pointers.
    bus_.on(kEvtSceneReplaced, [this](const std::any& data) {
        obj* const* op = std::any_cast<obj*>(&data);
        obj* newRoot = op ? *op : nullptr;
        if (scriptHost_) scriptHost_->unloadAll();
        // PlayMode audio cleanup: only active in Play-mode, but defensive.
        // (stopAllAudio was public; if not, not critical.)
        selection_.sanitize(newRoot);
    });
    bus_.emit(kEvtLogInfo,
              std::string("Editor started. Project: ") + projectPath_);

    // Phase 6a — auto-generate Lua API stubs (mtime-cache: no-op if
    // engine.ffi has NOT changed since the stored .luarc/engine.d.lua).
    if (!projectPath_.empty()) {
        generateLuaStubs(/*force=*/false);
    }
}

namespace {

void buildDefaultDockLayout(ImGuiID dockspace_id, ImVec2 size) {
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, size);

    ImGuiID main   = dockspace_id;
    ImGuiID top    = ImGui::DockBuilderSplitNode(main, ImGuiDir_Up,    0.05f, nullptr, &main);
    ImGuiID left   = ImGui::DockBuilderSplitNode(main, ImGuiDir_Left,  0.18f, nullptr, &main);
    ImGuiID right  = ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.22f, nullptr, &main);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down,  0.25f, nullptr, &main);

    ImGui::DockBuilderDockWindow("Toolbar",    top);
    ImGui::DockBuilderDockWindow("Hierarchy",  left);
    ImGui::DockBuilderDockWindow("Inspector",  right);
    ImGui::DockBuilderDockWindow("Project",    bottom);
    ImGui::DockBuilderDockWindow("Console",    bottom);
    ImGui::DockBuilderDockWindow("Build",      bottom);
    ImGui::DockBuilderDockWindow("Scene",      main);
    ImGui::DockBuilderDockWindow("Scene 2D",   main);
    ImGui::DockBuilderDockWindow("Game",       main);

    ImGui::DockBuilderFinish(dockspace_id);
}

}  // namespace

void EditorApp::drawDockHost() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##EditorDockHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("EditorDockSpace");
    if (needResetLayout_ || ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
        buildDefaultDockLayout(dockspace_id, vp->WorkSize);
        needResetLayout_ = false;
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0, 0),
                     ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}

void EditorApp::drawMenubar() {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
            openScene();
        }
        if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
            saveSceneAs();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Open Project...")) {
            std::string p = pickFolder("Open Project Folder");
            if (!p.empty()) {
                launchEditorWithProject(argv(0), p);
                quit();
            }
        }
        if (ImGui::MenuItem("Close Project")) {
            launchEditorWithProject(argv(0), {});
            quit();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            quit();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, commands_.canUndo())) {
            commands_.undo();
        }
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, commands_.canRedo())) {
            commands_.redo();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        for (auto& p : panels_) {
            ImGui::MenuItem(p->title().c_str(), nullptr, &p->visible);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout")) {
            resetDockLayout();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("GameObject")) {
        if (ImGui::MenuItem("Create Empty", "Ctrl+Shift+N")) {
            createEmpty();
        }
        if (ImGui::BeginMenu("3D Object")) {
            if (ImGui::MenuItem("Mesh (empty path)")) {
                createMesh("");
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("2D Object")) {
            if (ImGui::MenuItem("Sprite (empty path)")) {
                createSprite("");
            }
            if (ImGui::MenuItem("Tilemap (empty path)")) {
                createTilemap("");
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Light")) {
            if (ImGui::MenuItem("Directional")) createLight(0);
            if (ImGui::MenuItem("Point"))       createLight(1);
            if (ImGui::MenuItem("Spot"))        createLight(2);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Camera")) {
            if (ImGui::MenuItem("Camera")) createCamera();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Environment")) {
            if (ImGui::MenuItem("Fog")) createFogSettings();
            if (ImGui::MenuItem("Skybox (empty path)")) createSkybox("");
            if (ImGui::MenuItem("PostFX Stack")) createPostFXStack();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("UI")) {
            if (ImGui::MenuItem("Text (HUD)")) createTextRenderer("");
            if (ImGui::MenuItem("Text 3D (world-space)")) createText3DRenderer("");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Audio")) {
            if (ImGui::MenuItem("AudioSource (empty path)")) {
                createAudioSource("");
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Scripting")) {
            if (ImGui::MenuItem("Script (empty path)")) {
                // If there is a selection, make the Script a child
                // of it (so Lua `obj_parent(self)` points to that GameObject
                // and can read its Transform/MeshRenderer pos).
                createScript("", selection_.primary());
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools")) {
        const bool cooking = cookRunner_ && cookRunner_->isRunning();
        if (ImGui::MenuItem("Validate Assets", nullptr, false, !cooking)) {
            runAssetValidation();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Cook Assets (In-Place)", nullptr, false, !cooking)) {
            startCookInPlaceAllAssets();
        }
        if (ImGui::MenuItem("Build cook.zip...",      nullptr, false, !cooking)) {
            startBuildCookZip();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Cancel Cook", nullptr, false, cooking)) {
            requestCookCancel();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import Default FX Shaders")) {
            importDefaultFXShaders();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Copies the 28 bundled GLSL post-processing\n"
                "shaders into <project>/assets/fx/.\n"
                "Existing files are NOT overwritten — your edits are safe.");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Generate Lua API Stubs (force)")) {
            generateLuaStubs(true);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
        for (auto& p : panels_) {
            ImGui::MenuItem(p->title().c_str(), nullptr, &p->visible);
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void EditorApp::drawFrame() {
    ImGuizmo_BeginFrame();

    // Phase 5a — background-tasks (cook worker) → main-thread drain.
    // The bus.emit calls enqueued by the worker run here.
    mainQueue_.drainOnMainThread();
    if (cookRunner_) cookRunner_->joinIfDone();

    // Play-mode frame-tick: scripts on_update(dt) + mtime-poll auto-reload.
    // No-op in Edit mode.
    play_.frameTick(*this, app_delta());

    // Ctrl+Z / Ctrl+Y global undo/redo. We read from ImGui IO so we don't
    // need to hook into the engine-level `binding`.
    if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            commands_.undo();
            if (console_) console_->log("[Undo]");
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            commands_.redo();
            if (console_) console_->log("[Redo]");
        }
    }

    // W / E / R — gizmo-mode. Only when NOT editing a text field.
    if (!ImGui::GetIO().KeyCtrl && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) gizmoOp_ = 7;     // TRANSLATE
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) gizmoOp_ = 120;   // ROTATE
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) gizmoOp_ = 896;   // SCALE
    }

    // Ctrl+S — Save Scene quick-save (to lastSavedPath or saveSceneAs).
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        saveScene();
    }

    // We refresh the window-title every frame (the `*` modified-indicator and
    // the scene-basename can be of interest).
    refreshWindowTitle();

    drawMenubar();
    drawDockHost();
    for (auto& p : panels_) {
        p->draw(*this);
    }

    // Phase 5c — pre-cook validation modal (top-layer).
    drawCookValidationPopup();
}

void EditorApp::run() {
    // ESC-quit was intentionally removed (Phase 3e): in Scene panel freefly,
    // ESC serves cursor-restore. To exit: File → Exit, window-close button,
    // or `quit()`.
    while (app_swap() && !quit_) {
        drawFrame();
    }
}

}  // namespace editor
