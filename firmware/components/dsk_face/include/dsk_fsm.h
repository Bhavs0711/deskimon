#pragma once
/*
 * Deskimon emotion state machine.
 * Pure C (no LVGL / FreeRTOS) so it can be unit-tested on the host:
 *   gcc -I include tests/test_fsm.c dsk_fsm.c dsk_emotion.c
 *
 * Events drive transitions; the FSM never picks emotions randomly.
 */
#include <stdbool.h>
#include <stdint.h>
#include "dsk_emotion.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DSK_EV_BOOT = 0,
    DSK_EV_TOUCH,            /* any user touch / wake word / input (wakes the face)      */
    DSK_EV_LISTEN_START,     /* mic open                                                  */
    DSK_EV_LISTEN_STOP,      /* mic closed, nothing sent                                  */
    DSK_EV_CMD_SENT,         /* command forwarded to the Mac Agent -> thinking            */
    DSK_EV_PROCESSING,       /* Mac says it is working on it (long task)                  */
    DSK_EV_RESULT_OK,        /* arg = optional emotion hint (DSK_EMO_COUNT = none)        */
    DSK_EV_RESULT_ERR,       /* command failed                                            */
    DSK_EV_TASK_DONE,        /* user task completed -> proud then celebrating             */
    DSK_EV_SPEAK_START,
    DSK_EV_SPEAK_END,
    DSK_EV_ALERT,            /* alarm / meeting starting: alert until dismissed           */
    DSK_EV_DEADLINE_NEAR,    /* deadline approaching -> concerned                         */
    DSK_EV_DEADLINE_NOW,     /* deadline imminent -> deadline warning                     */
    DSK_EV_DISMISS,          /* user acknowledged an alert                                */
    DSK_EV_MAC_UP,
    DSK_EV_MAC_DOWN,
    DSK_EV_PAIRING,          /* pairing mode entered                                      */
    DSK_EV_PAIRED,           /* pairing completed                                         */
    DSK_EV_SHAKE,            /* IMU shake                                                 */
    DSK_EV_SET_EMOTION,      /* explicit request from Mac / BLE (arg + hold_ms)           */
    DSK_EV_BUSY,             /* long-running work shown as busy                           */
} dsk_event_t;

typedef struct {
    uint32_t idle_waiting_ms;     /* default 20 000  -> WAITING   */
    uint32_t idle_sleepy_ms;      /* default 90 000  -> SLEEPY    */
    uint32_t idle_sleeping_ms;    /* default 180 000 -> SLEEPING  */
    uint32_t disconnected_idle_ms;/* default 120 000: how long DISCONNECTED is shown before idle ladder */
    uint32_t activity_timeout_ms; /* default 45 000: LISTENING/THINKING/PROCESSING watchdog */
} dsk_fsm_cfg_t;

typedef struct {
    dsk_fsm_cfg_t cfg;
    dsk_emotion_t current;
    uint32_t entered_ms;
    uint32_t hold_ms;             /* 0 = until next event */
    uint32_t last_activity_ms;
    bool mac_connected;
    bool pairing;
    bool alert_active;
    /* optional chained follow-up (e.g. PROUD -> CELEBRATING) */
    bool has_chain;
    dsk_emotion_t chain_emo;
    uint32_t chain_hold_ms;
    /* last reason string for diagnostics */
    const char *last_reason;
} dsk_fsm_t;

void dsk_fsm_init(dsk_fsm_t *f, uint32_t now_ms);

/** Post an event. Returns true if the visible emotion changed. */
bool dsk_fsm_post(dsk_fsm_t *f, dsk_event_t ev, dsk_emotion_t arg, uint32_t hold_ms, uint32_t now_ms);

/** Call periodically (>= 4 Hz). Handles hold expiry, chains, watchdogs and the idle ladder. */
bool dsk_fsm_tick(dsk_fsm_t *f, uint32_t now_ms);

/** Emotion shown when nothing else is going on (pairing > disconnected > neutral). */
dsk_emotion_t dsk_fsm_rest_emotion(const dsk_fsm_t *f);

/** True for states where the backlight may be dimmed. */
bool dsk_emotion_is_sleepy(dsk_emotion_t e);

#ifdef __cplusplus
}
#endif
