// AudioSource — audio-component. In Play mode PlayMode activates each one:
// per AudioSource an `audio()` + `speaker()` + `speaker_play()`. In Stop
// mode every active speaker is `speaker_stop()`'d. In spatial mode the
// Scene panel's `renderScene` calls `speaker_position(pos)` every frame,
// and the listener gets the camera position via `listener_position`.

#include "engine.h"
#include "components_api.h"
#include "component_macros.h"

typedef struct AudioSource {
    OBJ
    COMPONENT_POS
    char  *clip_path;
    float  volume;       // 0..1
    float  pitch;        // 0.5..2 recommended
    int    loop;         // 0/1 (engine p2s has no bool)
    int    spatial;      // 0/1: if true → 3D pos-attenuation
} AudioSource;

OBJTYPEDEF(AudioSource, 70);

AUTORUN {
    STRUCT_POS(AudioSource);
    STRUCT(AudioSource, char*, clip_path, "[asset:clip]");
    STRUCT(AudioSource, float, volume,  "[range 0 1]");
    STRUCT(AudioSource, float, pitch,   "[range 0.5 2]");
    STRUCT(AudioSource, int,   loop,    "[bool]");
    STRUCT(AudioSource, int,   spatial, "[bool]");
}

obj* editor_obj_new_audio_source(obj* parent, const char* name,
                                 const char* clip_path) {
    AudioSource* a = obj_new_name(AudioSource, name ? name : "AudioSource");
    if (parent) obj_attach(parent, a);
    if (clip_path && *clip_path) a->clip_path = STRDUP(clip_path);
    a->volume = 1.0f;
    a->pitch  = 1.0f;
    a->loop   = 0;
    a->spatial = 1;
    return (obj*)a;
}

EDITOR_COMPONENT_POS_ONLY(AudioSource, audio_source)

const char* editor_audio_source_path(const obj* o) {
    if (!editor_obj_is_audio_source(o)) return NULL;
    return ((const AudioSource*)o)->clip_path;
}

void editor_audio_source_get_params(const obj* o,
                                    void* out_pos,
                                    float* out_volume,
                                    float* out_pitch,
                                    int* out_loop,
                                    int* out_spatial) {
    if (!editor_obj_is_audio_source(o)) return;
    const AudioSource* a = (const AudioSource*)o;
    if (out_pos)     *(vec3*)out_pos = a->pos;
    if (out_volume)  *out_volume = a->volume;
    if (out_pitch)   *out_pitch  = a->pitch;
    if (out_loop)    *out_loop   = a->loop;
    if (out_spatial) *out_spatial = a->spatial;
}
