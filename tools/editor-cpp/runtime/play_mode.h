#pragma once

#include <string>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor {

class EditorApp;

// Egy aktív audio speaker — a Play-szakaszban él.
struct PlayingSound {
    obj*      sourceNode;   // AudioSource node ref
    audio_t   clip;
    speaker_t spk;
    bool      spatial;
};

// Play-mode state-management. M11: snapshot + restore. M13: AudioSource
// indítás/leállás. M17: Script callback-ek (on_update/on_draw) később.
class PlayMode {
public:
    enum class State { Edit, Play, Pause };

    void start(EditorApp& app);   // snapshot + state=Play + audio + scripts
    void pause();                 // state=Pause (snapshot megmarad)
    void resume();                // state=Play
    void stop(EditorApp& app);    // restore snapshot + audio + scripts + state=Edit

    State state() const { return state_; }
    bool isEditing() const { return state_ == State::Edit; }
    bool isPlaying() const { return state_ == State::Play; }
    bool isPaused()  const { return state_ == State::Pause; }

    // A Scene panel render-walk-ja minden frame-ben felüt-frissíti a spatial
    // speaker-ek pozícióját, és a listener-pos a kamerát kapja.
    void updateAudio(EditorApp& app, const float listenerPos[3]);

    // Frame-tick: csak Play state-ben fut. ScriptHost::tickAll(dt) → on_update
    // minden Script-en + mtime-poll auto-reload. EditorApp::drawFrame elején
    // hívva (a Menubar/panel-draw előtt).
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
