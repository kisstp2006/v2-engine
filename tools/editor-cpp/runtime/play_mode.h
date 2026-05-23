#pragma once

#include <string>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor {

class EditorApp;

// One active audio speaker — lives during the Play phase.
struct PlayingSound {
    obj*      sourceNode;   // AudioSource node ref
    audio_t   clip;
    speaker_t spk;
    bool      spatial;
};

// Play-mode state-management. M11: snapshot + restore. M13: AudioSource
// start/stop. M17: Script callbacks (on_update/on_draw) later.
class PlayMode {
public:
    enum class State { Edit, Play, Pause };

    void start(EditorApp& app);   // snapshot + state=Play + audio + scripts
    void pause();                 // state=Pause (snapshot is kept)
    void resume();                // state=Play
    void stop(EditorApp& app);    // restore snapshot + audio + scripts + state=Edit

    State state() const { return state_; }
    bool isEditing() const { return state_ == State::Edit; }
    bool isPlaying() const { return state_ == State::Play; }
    bool isPaused()  const { return state_ == State::Pause; }

    // The Scene panel's render-walk refreshes the position of the spatial
    // speakers every frame, and the listener-pos receives the camera.
    void updateAudio(EditorApp& app, const float listenerPos[3]);

    // Frame-tick: only runs in the Play state. ScriptHost::tickAll(dt) →
    // on_update on every Script + mtime-poll auto-reload. Called at the
    // start of EditorApp::drawFrame (before the Menubar/panel-draw).
    void frameTick(EditorApp& app, float dt);

private:
    void startAudioFor(EditorApp& app, obj* node);
    void collectAndStartAudio(EditorApp& app, obj* node);
    void stopAllAudio();

    State                     state_ = State::Edit;
    std::string               snapshot_;
    std::vector<PlayingSound> playing_;
};

}  // namespace editor
