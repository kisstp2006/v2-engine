// STL ELŐSZÖR.
#include <string>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "play_mode.h"
#include "../app/editor_app.h"
#include "../components/components_api.h"
#include "../core/event_bus.h"
#include "../core/asset_path.h"
#include "../core/selection_service.h"
#include "../persistence/scene_io.h"
#include "../scene/scene_helpers.h"
#include "../scene/scene_service.h"
#include "script_host.h"

namespace editor {

void PlayMode::startAudioFor(EditorApp& app, obj* node) {
    const char* relPath = editor_audio_source_path(node);
    if (!relPath || !*relPath) return;
    // Phase 4a — abs-resolve.
    std::string absPath = asset_path::toAbsolute(relPath, app.projectPath());
    if (!is_file(absPath.c_str())) return;

    int len = 0;
    char* content = file_read(absPath.c_str(), &len);
    if (!content || len <= 0) return;

    audio_t clip = audio(content, len, AUDIO_CLIP);
    if (clip.err) return;

    speaker_t spk = speaker();
    int loop = 0, spatial = 1;
    float volume = 1.0f, pitch = 1.0f;
    vec3 pos;
    editor_audio_source_get_params(node, &pos, &volume, &pitch,
                                   &loop, &spatial);

    speaker_gain(&spk, volume);
    speaker_pitch(&spk, pitch);
    speaker_loop(&spk, loop != 0);
    if (spatial) {
        float p[3] = { pos.x, pos.y, pos.z };
        speaker_position(&spk, p, false);
    }
    speaker_play(&spk, clip, false);

    PlayingSound s;
    s.sourceNode = node;
    s.clip = clip;
    s.spk = spk;
    s.spatial = (spatial != 0);
    playing_.push_back(s);
}

void PlayMode::collectAndStartAudio(EditorApp& app, obj* node) {
    if (!node) return;
    if (editor_obj_is_audio_source(node)) startAudioFor(app, node);
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        collectAndStartAudio(app, editor_obj_child_at(node, i));
    }
}

void PlayMode::stopAllAudio() {
    for (auto& s : playing_) {
        // speaker_stop deklarált a motor-headerben, de implementálatlan.
        // A speaker_destroy belül speaker_unbind + alDeleteSources → leáll.
        speaker_destroy(&s.spk);
        audio_destroy(&s.clip);
    }
    playing_.clear();
}

void PlayMode::updateAudio(EditorApp& app, const float listenerPos[3]) {
    if (state_ != State::Play) return;
    (void)app;
    if (listenerPos) listener_position(listenerPos);
    for (auto& s : playing_) {
        if (!s.spatial) continue;
        vec3* p = editor_audio_source_pos_addr(s.sourceNode);
        if (!p) continue;
        float ap[3] = { p->x, p->y, p->z };
        speaker_position(&s.spk, ap, false);
    }
}

void PlayMode::start(EditorApp& app) {
    if (state_ == State::Play) return;
    // Snapshot a teljes scene-tree-ről (reflection-vezérelten).
    snapshot_ = SceneIO::saveTree(app.scene().root());
    state_ = State::Play;
    collectAndStartAudio(app, app.scene().root());
    app.scriptHost().startAll();   // per-Script lua_State + on_init
    app.bus().emit("log", std::string("[Play] started"));
}

void PlayMode::frameTick(EditorApp& app, float dt) {
    if (state_ != State::Play) return;
    app.scriptHost().tickAll(dt);    // on_update + mtime-poll auto-reload
}

void PlayMode::pause() {
    if (state_ != State::Play) return;
    state_ = State::Pause;
}

void PlayMode::resume() {
    if (state_ != State::Pause) return;
    state_ = State::Play;
}

void PlayMode::stop(EditorApp& app) {
    if (state_ == State::Edit) return;
    // SORREND szigorú: 1) on_quit + lua_close MIELŐTT a scene-tree-t eldobjuk
    // (különben dangling obj* a ScriptHost map-jében). 2) audio-stop. 3) restore.
    app.scriptHost().stopAll();
    stopAllAudio();
    // Restore a snapshot-ból. A jelenlegi scene-tree-t eldobjuk.
    if (!snapshot_.empty()) {
        obj* newRoot = SceneIO::loadTree(snapshot_);
        if (newRoot) {
            app.scene().replaceRoot(newRoot);
            app.selection().clear();
        }
    }
    snapshot_.clear();
    state_ = State::Edit;
    app.bus().emit("log", std::string("[Play] stopped, restored"));
}

}  // namespace editor
