#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tal_api.h"
#include "audio_test.h"
#include "eye_scene.h"
#include "lvgl.h"
#include "voice_bridge.h"
#include "eye_layer_renderer.h"
#include "eye_scenes.h"
#include "eye_scene_spec.h"

#define DISPLAY_W       ((int)lv_disp_get_hor_res(NULL))
#define DISPLAY_H       ((int)lv_disp_get_ver_res(NULL))
#define TIMER_PERIOD_MS 33
#define UI_ROTATION     LV_DISP_ROT_270

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef struct {
    EYE_LAYER_RENDERER_T rend;
    lv_obj_t          *canvas;
    lv_timer_t        *timer;
    MUTEX_HANDLE       lock;
    EYE_EMOTION_E      base_emotion;
    EYE_EMOTION_E      override_emotion;
    uint32_t           override_start_ms;
    uint32_t           override_until_ms;
    bool               motion_enabled;
    EYE_MOTION_STATE_T motion;
    float              smooth_gaze_x;
    float              smooth_gaze_y;
    float              natural_gaze_x;
    float              natural_gaze_y;
    uint32_t           next_natural_gaze_ms;
    uint32_t           next_blink_ms;
    uint32_t           blink_start_ms;
    uint32_t           blink_duration_ms;
    bool               blink_double_pending;
    RUNTIME_STATE_T    runtime;
} EYE_SCENE_CTX_T;

typedef struct {
    const char   *name;
    EYE_EMOTION_E emotion;
} EMOTION_MAP_T;

static const EMOTION_MAP_T cEMOTION_MAP[] = {
    {"IDLE", EYE_EMOTION_IDLE},
    {"HAPPY", EYE_EMOTION_HAPPY},
    {"SAD", EYE_EMOTION_SAD},
    {"ANGRY_SOFT", EYE_EMOTION_ANGRY_SOFT},
    {"ANGRY_HARD", EYE_EMOTION_ANGRY_HARD},
    {"ANGRY", EYE_EMOTION_ANGRY_HARD},
    {"WOW", EYE_EMOTION_WOW},
    {"SLEEPY", EYE_EMOTION_SLEEPY},
    {"CONFUSED", EYE_EMOTION_CONFUSED},
    {"MAIL", EYE_EMOTION_CONFUSED},
    {"DIZZY", EYE_EMOTION_DIZZY},
    {"CALL", EYE_EMOTION_CALL},
    {"SHAKE", EYE_EMOTION_SHAKE},
    {"WINK", EYE_EMOTION_WINK},
    {"XX", EYE_EMOTION_XX},
    {"GLITCH", EYE_EMOTION_GLITCH},
    {"BORED", EYE_EMOTION_BORED},
    {"FOCUSED", EYE_EMOTION_FOCUSED},
    {"HAPPY_TONGUE", EYE_EMOTION_HAPPY_TONGUE},
    {"HOLLOW", EYE_EMOTION_HOLLOW},
    {"LISTEN", EYE_EMOTION_LISTENING},
    {"LISTENING", EYE_EMOTION_LISTENING},
    {"THINK", EYE_EMOTION_THINKING},
    {"THINKING", EYE_EMOTION_THINKING},
    {"CURIOUS", EYE_EMOTION_CURIOUS},
    {"DELIGHTED", EYE_EMOTION_DELIGHTED},
    {"JOY", EYE_EMOTION_DELIGHTED},
    {"SHY", EYE_EMOTION_SHY},
    {"SASSY", EYE_EMOTION_SASSY},
    {"SMUG", EYE_EMOTION_SASSY},
    {"PROUD", EYE_EMOTION_PROUD},
    {"SPEAK", EYE_EMOTION_SPEAKING},
    {"SPEAKING", EYE_EMOTION_SPEAKING},
};

static EYE_SCENE_CTX_T sg_ctx = {0};

static float __clampf(float value, float min_v, float max_v)
{
    if (value < min_v) return min_v;
    if (value > max_v) return max_v;
    return value;
}

static float __randf(float min_v, float max_v)
{
    return min_v + (((float)rand() / (float)RAND_MAX) * (max_v - min_v));
}

static uint32_t __rand_ms(uint32_t min_v, uint32_t max_v)
{
    if (max_v <= min_v) return min_v;
    return min_v + (uint32_t)(rand() % (int)(max_v - min_v + 1));
}

const char *eye_scene_emotion_name(EYE_EMOTION_E emotion)
{
    switch (emotion) {
    case EYE_EMOTION_IDLE: return "IDLE";
    case EYE_EMOTION_HAPPY: return "HAPPY";
    case EYE_EMOTION_SAD: return "SAD";
    case EYE_EMOTION_ANGRY_SOFT: return "ANGRY_SOFT";
    case EYE_EMOTION_ANGRY_HARD: return "ANGRY_HARD";
    case EYE_EMOTION_WOW: return "WOW";
    case EYE_EMOTION_SLEEPY: return "SLEEPY";
    case EYE_EMOTION_CONFUSED: return "CONFUSED";
    case EYE_EMOTION_DIZZY: return "DIZZY";
    case EYE_EMOTION_CALL: return "CALL";
    case EYE_EMOTION_SHAKE: return "SHAKE";
    case EYE_EMOTION_WINK: return "WINK";
    case EYE_EMOTION_XX: return "XX";
    case EYE_EMOTION_GLITCH: return "GLITCH";
    case EYE_EMOTION_BORED: return "BORED";
    case EYE_EMOTION_FOCUSED: return "FOCUSED";
    case EYE_EMOTION_HAPPY_TONGUE: return "HAPPY_TONGUE";
    case EYE_EMOTION_HOLLOW: return "HOLLOW";
    case EYE_EMOTION_LISTENING: return "LISTENING";
    case EYE_EMOTION_THINKING: return "THINKING";
    case EYE_EMOTION_CURIOUS: return "CURIOUS";
    case EYE_EMOTION_DELIGHTED: return "DELIGHTED";
    case EYE_EMOTION_SHY: return "SHY";
    case EYE_EMOTION_SASSY: return "SASSY";
    case EYE_EMOTION_PROUD: return "PROUD";
    case EYE_EMOTION_SPEAKING: return "SPEAKING";
    default: return "IDLE";
    }
}

static const EYE_SCENE_SPEC_T * __get_scene_spec(EYE_EMOTION_E emotion) {
    // If a scene isn't defined or we couldn't port it, fallback to kEveBaseScene / kEveIdleScene
    switch (emotion) {
        case EYE_EMOTION_HAPPY: return &kEveHappyScene;
        case EYE_EMOTION_SAD: return &kEveSadScene;
        case EYE_EMOTION_ANGRY_SOFT: return &kEveAngrySoftScene;
        case EYE_EMOTION_ANGRY_HARD: return &kEveAngryHardScene;
        case EYE_EMOTION_WOW: return &kEveWowScene;
        case EYE_EMOTION_SLEEPY: return &kEveSleepyScene;
        case EYE_EMOTION_DIZZY: return &kEveDizzyScene;
        case EYE_EMOTION_XX: return &kEveXXScene;
        case EYE_EMOTION_GLITCH: return &kEveGlitchScene;
        case EYE_EMOTION_WINK: return &kEveWinkScene;
        case EYE_EMOTION_BORED: return &kEveBoredScene;
        case EYE_EMOTION_FOCUSED: return &kEveFocusedScene;
        case EYE_EMOTION_HOLLOW: return &kEveSunglassesRoundScene;
        case EYE_EMOTION_SHAKE: return &kEveShakeScene;
        case EYE_EMOTION_CALL: return &kEveCallScene;
        case EYE_EMOTION_CONFUSED: return &kEveConfusedScene;
        case EYE_EMOTION_HAPPY_TONGUE: return &kEveHappyTongueScene;
        
        // Mappings for the extended emotional profile aliases requested by UI
        case EYE_EMOTION_DELIGHTED: return &kEveExcitedScene;
        case EYE_EMOTION_LISTENING: return &kEveFocusedScene;
        case EYE_EMOTION_THINKING: return &kEveFocusedScene;
        case EYE_EMOTION_CURIOUS: return &kEveConfusedScene;
        case EYE_EMOTION_SHY: return &kEveHappyScene;
        case EYE_EMOTION_SASSY: return &kEveAngrySoftScene;
        case EYE_EMOTION_PROUD: return &kEveHappyScene;
        case EYE_EMOTION_SPEAKING: return &kEveIdleScene;

        case EYE_EMOTION_IDLE: 
        default: return &kEveIdleScene;
    }
}

static void __update_animation_profile(const ANIMATION_PROFILE_T *anim, uint32_t now_ms)
{
    if (!anim->drift.enabled) {
        sg_ctx.natural_gaze_x *= 0.75f;
        sg_ctx.natural_gaze_y *= 0.75f;
        sg_ctx.next_natural_gaze_ms = 0;
    } else {
        if (0 == sg_ctx.next_natural_gaze_ms || now_ms >= sg_ctx.next_natural_gaze_ms) {
            bool is_saccade = ((rand() % 100) < 70); // 70% darting
            float boost_factor = 25.0f; // Make them dart aggressively by up to ~75 pixels

            // Randomize X and Y independently to create huge, expressive bounds
            if (is_saccade) {
                sg_ctx.natural_gaze_x = __randf(-anim->drift.saccade_x_amplitude * boost_factor, anim->drift.saccade_x_amplitude * boost_factor);
                sg_ctx.natural_gaze_y = __randf(-anim->drift.saccade_y_amplitude * boost_factor, anim->drift.saccade_y_amplitude * boost_factor);
            } else {
                sg_ctx.natural_gaze_x = __randf(-anim->drift.drift_x_amplitude * boost_factor, anim->drift.drift_x_amplitude * boost_factor);
                sg_ctx.natural_gaze_y = __randf(-anim->drift.drift_y_amplitude * boost_factor, anim->drift.drift_y_amplitude * boost_factor);
            }
            // Fire very frequently to keep it alive and bouncing (between 500ms and 1.8 seconds)
            sg_ctx.next_natural_gaze_ms = now_ms + __rand_ms(500, 1800);
        }
    }

    if (!anim->blink.enabled) {
        sg_ctx.blink_start_ms = 0;
        sg_ctx.blink_duration_ms = 0;
        sg_ctx.runtime.blink = false;
        sg_ctx.next_blink_ms = 0;
    } else {
        if (0 == sg_ctx.next_blink_ms) {
            // Blink consistently but randomly between 1.5 and 4.5 seconds
            sg_ctx.next_blink_ms = now_ms + __rand_ms(1500, 4500);
        }

        if (0 == sg_ctx.blink_start_ms && now_ms >= sg_ctx.next_blink_ms) {
            sg_ctx.blink_start_ms = now_ms;
            // Native blink is 140ms. Boost to 350ms so it has a cute, defined "squish" that you can easily see over 30fps
            sg_ctx.blink_duration_ms = anim->blink.duration_ms + 210;
        }

        if (0 != sg_ctx.blink_start_ms) {
            uint32_t elapsed = now_ms - sg_ctx.blink_start_ms;
            if (elapsed >= sg_ctx.blink_duration_ms) {
                sg_ctx.blink_start_ms = 0;
                sg_ctx.blink_duration_ms = 0;
                sg_ctx.runtime.blink = false;
                sg_ctx.next_blink_ms = now_ms + __rand_ms(1500, 4500);
            } else {
                float progress = (float)elapsed / (float)sg_ctx.blink_duration_ms;
                // Exaggerate the easing curve for a snappy blink
                float blend = 1.0f - fabsf((progress * 2.0f) - 1.0f);
                sg_ctx.runtime.blink = blend > 0.05f;
                // Clamp it down to exactly the closed height almost instantly to give that robotic square-wave blink feel
                sg_ctx.runtime.blink_height = anim->blink.closed_height;
            }
        }
    }

    if (anim->pulse.enabled && anim->pulse.period_ms > 0) {
        sg_ctx.runtime.pulse_shift = (int8_t)(sinf((float)now_ms * 3.14159f * 2.0f / (float)anim->pulse.period_ms) * anim->pulse.amplitude);
    } else {
        sg_ctx.runtime.pulse_shift = 0;
    }
}

static void __scene_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now_ms = tal_system_get_millisecond();

    tal_mutex_lock(sg_ctx.lock);
    EYE_EMOTION_E emotion = sg_ctx.base_emotion;
    if (sg_ctx.override_until_ms != 0 && now_ms <= sg_ctx.override_until_ms) {
        emotion = sg_ctx.override_emotion;
    } else {
        sg_ctx.override_until_ms = 0;
    }
    EYE_MOTION_STATE_T motion = sg_ctx.motion;
    bool motion_enabled = sg_ctx.motion_enabled;
    tal_mutex_unlock(sg_ctx.lock);

    if (motion_enabled && motion.activity > 1.5f) {
        // Device is being aggressively shaken
        eye_scene_set_emotion(EYE_EMOTION_DIZZY, 2500);
        emotion = EYE_EMOTION_DIZZY;
    }

    const EYE_SCENE_SPEC_T *scene = __get_scene_spec(emotion);

    __update_animation_profile(&scene->animation, now_ms);

    float tilt_gaze_x = 0.0f;
    float tilt_gaze_y = 0.0f;
    
    if (motion_enabled && motion.present) {
        tilt_gaze_x = __clampf(motion.tilt_x * 20.0f, -20.0f, 20.0f);
        tilt_gaze_y = __clampf(-motion.tilt_y * 15.0f, -15.0f, 15.0f);
    }

    sg_ctx.runtime.eye_color = lv_color_hex(0x0FDAFF); // Cyan
    if (emotion == EYE_EMOTION_GLITCH) sg_ctx.runtime.eye_color = lv_color_hex(0xFB7185);

    sg_ctx.smooth_gaze_x += (sg_ctx.natural_gaze_x + tilt_gaze_x - sg_ctx.smooth_gaze_x) * 0.34f;
    sg_ctx.smooth_gaze_y += (sg_ctx.natural_gaze_y + tilt_gaze_y - sg_ctx.smooth_gaze_y) * 0.34f;

    sg_ctx.runtime.saccade_x = (int16_t)sg_ctx.smooth_gaze_x;
    sg_ctx.runtime.saccade_y = (int16_t)sg_ctx.smooth_gaze_y;
    
    if (emotion == EYE_EMOTION_GLITCH) {
        sg_ctx.runtime.glitch_row_shift = true;
        sg_ctx.runtime.glitch_row_y = (now_ms % 240);
        sg_ctx.runtime.glitch_row_h = 10;
        sg_ctx.runtime.glitch_row_offset = (now_ms % 100 < 50) ? 10 : -10;
        sg_ctx.runtime.glitch_scanline_count = 2;
        sg_ctx.runtime.glitch_scanline_y[0] = (now_ms / 2) % 240;
        sg_ctx.runtime.glitch_scanline_h[0] = 4;
        sg_ctx.runtime.glitch_scanline_y[1] = (now_ms / 3 + 120) % 240;
        sg_ctx.runtime.glitch_scanline_h[1] = 6;
    } else {
        sg_ctx.runtime.glitch_row_shift = false;
        sg_ctx.runtime.glitch_scanline_count = 0;
    }

    eye_layer_renderer_render_eye(&sg_ctx.rend, scene, &sg_ctx.runtime);

    // Single unified canvas exactly centers the single 466x466 mask perfectly
    lv_obj_align(sg_ctx.canvas, LV_ALIGN_CENTER, 0, 0);
}


static int __default_hold_ms(EYE_EMOTION_E emotion)
{
    switch (emotion) {
    case EYE_EMOTION_WINK: return 700;
    case EYE_EMOTION_SHAKE: return 1300;
    case EYE_EMOTION_GLITCH: return 2000;
    case EYE_EMOTION_CALL: return 2500;
    case EYE_EMOTION_WOW: return 2000;
    case EYE_EMOTION_CONFUSED:
    case EYE_EMOTION_DIZZY:
    case EYE_EMOTION_XX: return 2400;
    case EYE_EMOTION_DELIGHTED:
    case EYE_EMOTION_CURIOUS:
    case EYE_EMOTION_SHY:
    case EYE_EMOTION_SASSY:
    case EYE_EMOTION_PROUD: return 3400;
    case EYE_EMOTION_LISTENING:
    case EYE_EMOTION_THINKING:
    case EYE_EMOTION_SPEAKING: return 2800;
    default: return 0;
    }
}

static EYE_EMOTION_E __parse_emotion(const char *token)
{
    for (uint32_t i = 0; i < ARRAY_SIZE(cEMOTION_MAP); i++) {
        if (0 == strcmp(token, cEMOTION_MAP[i].name)) {
            return cEMOTION_MAP[i].emotion;
        }
    }
    return EYE_EMOTION_MAX;
}

static void __normalize_text(const char *src, char *dst, uint32_t dst_len)
{
    uint32_t i = 0;
    if (0 == dst_len) return;
    for (; i + 1 < dst_len && src[i] != '\0'; i++) {
        char ch = src[i];
        if ('-' == ch) ch = '_';
        dst[i] = (char)toupper((unsigned char)ch);
    }
    dst[i] = '\0';
}

static void __trim(char *text)
{
    char *start = text;
    while (*start != '\0' && isspace((unsigned char)*start)) start++;
    if (start != text) memmove(text, start, strlen(start) + 1);
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
}

OPERATE_RET eye_scene_set_emotion(EYE_EMOTION_E emotion, uint32_t hold_ms)
{
    uint32_t now_ms = tal_system_get_millisecond();
    if (emotion >= EYE_EMOTION_MAX) return OPRT_INVALID_PARM;

    tal_mutex_lock(sg_ctx.lock);
    if (hold_ms > 0) {
        sg_ctx.override_emotion  = emotion;
        sg_ctx.override_start_ms = now_ms;
        sg_ctx.override_until_ms = now_ms + hold_ms;
    } else {
        sg_ctx.base_emotion      = emotion;
        sg_ctx.override_until_ms = 0;
        sg_ctx.override_start_ms = 0;
    }
    tal_mutex_unlock(sg_ctx.lock);
    return OPRT_OK;
}

OPERATE_RET eye_scene_set_motion_state(const EYE_MOTION_STATE_T *motion)
{
    if (NULL == motion || NULL == sg_ctx.lock) return OPRT_INVALID_PARM;
    tal_mutex_lock(sg_ctx.lock);
    sg_ctx.motion = *motion;
    tal_mutex_unlock(sg_ctx.lock);
    return OPRT_OK;
}

OPERATE_RET eye_scene_process_command(const char *cmd, char *reply, uint32_t reply_len)
{
    char          work[128] = {0};
    char          norm[128] = {0};
    char         *token     = NULL;
    char         *saveptr   = NULL;
    EYE_EMOTION_E emotion   = EYE_EMOTION_MAX;
    uint32_t      hold_ms   = 0;

    if (NULL == cmd || NULL == reply || 0 == reply_len) return OPRT_INVALID_PARM;
    snprintf(work, sizeof(work), "%s", cmd);
    __trim(work);
    __normalize_text(work, norm, sizeof(norm));

    if ('\0' == norm[0]) {
        snprintf(reply, reply_len, "ERR EMPTY");
        return OPRT_INVALID_PARM;
    }

    if (0 == strcmp(norm, "PING")) {
        snprintf(reply, reply_len, "PONG");
        return OPRT_OK;
    }

    if (0 == strcmp(norm, "STATUS")) {
        EYE_EMOTION_E active = (sg_ctx.override_until_ms != 0 && tal_system_get_millisecond() <= sg_ctx.override_until_ms)
                             ? sg_ctx.override_emotion : sg_ctx.base_emotion;
        snprintf(reply, reply_len, "OK STATE=%s MOTION=%s IMU=%s", eye_scene_emotion_name(active),
                 sg_ctx.motion_enabled ? "ON" : "OFF", sg_ctx.motion.present ? "ON" : "OFF");
        return OPRT_OK;
    }

    if (0 == strcmp(norm, "HELP")) {
        snprintf(reply, reply_len, "OK CMDS=PING|STATUS|BEEP|VOICE ...|MOTION ON|MOTION OFF|<EMOTION>|HOLD <EMOTION> <MS>");
        return OPRT_OK;
    }

    token = strtok_r(norm, " ", &saveptr);
    if (NULL == token) return OPRT_INVALID_PARM;

    if (0 == strcmp(token, "MOTION")) {
        char *val = strtok_r(NULL, " ", &saveptr);
        if (!val) return OPRT_INVALID_PARM;
        sg_ctx.motion_enabled = (0 == strcmp(val, "ON"));
        snprintf(reply, reply_len, "OK MOTION %s", sg_ctx.motion_enabled ? "ON" : "OFF");
        return OPRT_OK;
    }

    if (0 == strcmp(token, "BEEP") || 0 == strcmp(token, "SPKTEST") || 0 == strcmp(token, "SPEAKER")) {
        audio_test_play_beep();
        snprintf(reply, reply_len, "OK BEEP");
        return OPRT_OK;
    }

    if (0 == strcmp(token, "VOICE")) {
        return voice_bridge_process_command(saveptr, reply, reply_len);
    }

    if (0 == strcmp(token, "HOLD")) {
        char *emo_name = strtok_r(NULL, " ", &saveptr);
        char *ms_tok = strtok_r(NULL, " ", &saveptr);
        if (!emo_name) return OPRT_INVALID_PARM;
        emotion = __parse_emotion(emo_name);
        hold_ms = ms_tok ? (uint32_t)strtoul(ms_tok, NULL, 10) : __default_hold_ms(emotion);
        eye_scene_set_emotion(emotion, hold_ms ? hold_ms : 1200);
        snprintf(reply, reply_len, "OK %s HOLD=%lu", eye_scene_emotion_name(emotion), (unsigned long)hold_ms);
        return OPRT_OK;
    }

    if (0 == strcmp(token, "SET")) {
        token = strtok_r(NULL, " ", &saveptr);
    }
    if (0 == strncmp(token, "EMOTION=", 8)) token += 8;
    
    emotion = __parse_emotion(token);
    if (emotion >= EYE_EMOTION_MAX) {
        snprintf(reply, reply_len, "ERR EMOTION");
        return OPRT_INVALID_PARM;
    }

    hold_ms = __default_hold_ms(emotion);
    eye_scene_set_emotion(emotion, hold_ms);
    snprintf(reply, reply_len, hold_ms ? "OK %s HOLD=%lu" : "OK %s", eye_scene_emotion_name(emotion), (unsigned long)hold_ms);
    return OPRT_OK;
}

static void __screen_touch_cb(lv_event_t * e) {
    (void)e;
    // Petting interaction
    eye_scene_set_emotion(EYE_EMOTION_HAPPY_TONGUE, 2500);
}

OPERATE_RET eye_scene_init(void)
{
    lv_disp_t  *disp   = lv_disp_get_default();
    lv_obj_t   *screen = NULL;

    if (NULL == sg_ctx.lock) {
        tal_mutex_create_init(&sg_ctx.lock);
    }

    sg_ctx.base_emotion      = EYE_EMOTION_IDLE;
    sg_ctx.motion_enabled    = true;

    srand((unsigned int)tal_system_get_millisecond());

    if (NULL != disp) {
        lv_disp_set_rotation(disp, UI_ROTATION);
    }

    screen = lv_scr_act();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, __screen_touch_cb, LV_EVENT_PRESSED, NULL);

    sg_ctx.canvas = lv_canvas_create(screen);
    eye_layer_renderer_init(&sg_ctx.rend, sg_ctx.canvas);

    sg_ctx.timer = lv_timer_create(__scene_timer_cb, TIMER_PERIOD_MS, NULL);
    __scene_timer_cb(sg_ctx.timer);

    return OPRT_OK;
}
