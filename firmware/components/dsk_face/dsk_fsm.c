#include <string.h>
#include "dsk_fsm.h"

typedef enum { PRI_IDLE = 0, PRI_ACTIVITY = 1, PRI_REACTION = 2, PRI_URGENT = 3 } pri_t;

static pri_t pri_of(dsk_emotion_t e)
{
    switch (e) {
    case DSK_EMO_ALERT: case DSK_EMO_DEADLINE_WARNING:
        return PRI_URGENT;
    case DSK_EMO_SUCCESS: case DSK_EMO_ERROR: case DSK_EMO_HAPPY: case DSK_EMO_VERY_HAPPY:
    case DSK_EMO_PROUD: case DSK_EMO_CELEBRATING: case DSK_EMO_EXCITED: case DSK_EMO_SURPRISED:
    case DSK_EMO_CONCERNED: case DSK_EMO_WORRIED: case DSK_EMO_DISAPPOINTED: case DSK_EMO_MOTIVATING:
    case DSK_EMO_CONFUSED: case DSK_EMO_CURIOUS:
        return PRI_REACTION;
    case DSK_EMO_LISTENING: case DSK_EMO_THINKING: case DSK_EMO_PROCESSING: case DSK_EMO_SPEAKING:
    case DSK_EMO_BUSY: case DSK_EMO_FOCUSED:
        return PRI_ACTIVITY;
    default:
        return PRI_IDLE;
    }
}

bool dsk_emotion_is_sleepy(dsk_emotion_t e)
{
    return e == DSK_EMO_SLEEPY || e == DSK_EMO_SLEEPING;
}

static bool is_idle_state(dsk_emotion_t e)
{
    return e == DSK_EMO_NEUTRAL || e == DSK_EMO_CALM || e == DSK_EMO_WAITING || e == DSK_EMO_SLEEPY ||
           e == DSK_EMO_SLEEPING || e == DSK_EMO_TIRED || e == DSK_EMO_DISCONNECTED || e == DSK_EMO_PAIRING;
}

void dsk_fsm_init(dsk_fsm_t *f, uint32_t now)
{
    memset(f, 0, sizeof(*f));
    f->cfg = (dsk_fsm_cfg_t){
        .idle_waiting_ms = 20000, .idle_sleepy_ms = 90000, .idle_sleeping_ms = 180000,
        .disconnected_idle_ms = 120000, .activity_timeout_ms = 45000,
    };
    f->current = DSK_EMO_DISCONNECTED;
    f->entered_ms = now;
    f->last_activity_ms = now;
    f->last_reason = "init";
}

dsk_emotion_t dsk_fsm_rest_emotion(const dsk_fsm_t *f)
{
    if (f->pairing) return DSK_EMO_PAIRING;
    if (!f->mac_connected) return DSK_EMO_DISCONNECTED;
    return DSK_EMO_NEUTRAL;
}

static bool enter(dsk_fsm_t *f, dsk_emotion_t e, uint32_t hold, uint32_t now, const char *why)
{
    bool changed = (e != f->current);
    f->current = e;
    f->entered_ms = now;
    f->hold_ms = hold;
    f->has_chain = false;
    f->last_reason = why;
    return changed;
}

/* Apply with priority gating: lower-priority events never preempt an active higher one. */
static bool request(dsk_fsm_t *f, dsk_emotion_t e, uint32_t hold, uint32_t now, const char *why)
{
    bool cur_active = f->hold_ms == 0 ? !is_idle_state(f->current)
                                      : (now - f->entered_ms) < f->hold_ms;
    if (f->alert_active && pri_of(e) < PRI_URGENT) return false;       /* alert only ends via DISMISS */
    if (cur_active && pri_of(e) < pri_of(f->current)) return false;
    return enter(f, e, hold, now, why);
}

static bool back_to_rest(dsk_fsm_t *f, uint32_t now, const char *why)
{
    f->alert_active = false;
    return enter(f, dsk_fsm_rest_emotion(f), 0, now, why);
}

bool dsk_fsm_post(dsk_fsm_t *f, dsk_event_t ev, dsk_emotion_t arg, uint32_t hold, uint32_t now)
{
    bool ch = false;
    switch (ev) {
    case DSK_EV_BOOT:
        f->last_activity_ms = now;
        ch = enter(f, dsk_fsm_rest_emotion(f), 0, now, "boot");
        break;

    case DSK_EV_TOUCH:
        f->last_activity_ms = now;
        if (f->alert_active) break;
        if (dsk_emotion_is_sleepy(f->current) || f->current == DSK_EMO_WAITING || f->current == DSK_EMO_TIRED)
            ch = enter(f, DSK_EMO_ALERT, 700, now, "wake");       /* input detected -> Alert */
        break;

    case DSK_EV_LISTEN_START:
        f->last_activity_ms = now;
        f->alert_active = false;
        ch = enter(f, DSK_EMO_LISTENING, 0, now, "listen");
        break;

    case DSK_EV_LISTEN_STOP:
        f->last_activity_ms = now;
        if (f->current == DSK_EMO_LISTENING) ch = back_to_rest(f, now, "listen stop");
        break;

    case DSK_EV_CMD_SENT:
        f->last_activity_ms = now;
        ch = request(f, DSK_EMO_THINKING, 0, now, "command sent");
        break;

    case DSK_EV_PROCESSING:
        f->last_activity_ms = now;
        ch = request(f, DSK_EMO_PROCESSING, 0, now, "processing");
        break;

    case DSK_EV_BUSY:
        f->last_activity_ms = now;
        ch = request(f, DSK_EMO_BUSY, hold ? hold : 0, now, "busy");
        break;

    case DSK_EV_RESULT_OK: {
        f->last_activity_ms = now;
        dsk_emotion_t e = (arg < DSK_EMO_COUNT && arg != DSK_EMO_NEUTRAL) ? arg : DSK_EMO_SUCCESS;
        /* Result reactions must be allowed to replace THINKING/PROCESSING */
        f->hold_ms = 1; f->entered_ms = now - 2;
        ch = request(f, e, hold ? hold : 2600, now, "result ok");
        break;
    }

    case DSK_EV_RESULT_ERR:
        f->last_activity_ms = now;
        f->hold_ms = 1; f->entered_ms = now - 2;
        ch = request(f, DSK_EMO_ERROR, hold ? hold : 3200, now, "result error");
        if (f->current == DSK_EMO_ERROR) { f->has_chain = true; f->chain_emo = DSK_EMO_CONFUSED; f->chain_hold_ms = 2200; }
        break;

    case DSK_EV_TASK_DONE:
        f->last_activity_ms = now;
        f->hold_ms = 1; f->entered_ms = now - 2;
        ch = request(f, DSK_EMO_PROUD, 1800, now, "task done");
        if (f->current == DSK_EMO_PROUD) { f->has_chain = true; f->chain_emo = DSK_EMO_CELEBRATING; f->chain_hold_ms = 2800; }
        break;

    case DSK_EV_SPEAK_START:
        f->last_activity_ms = now;
        f->hold_ms = 1; f->entered_ms = now - 2;
        ch = request(f, DSK_EMO_SPEAKING, 0, now, "speak");
        break;

    case DSK_EV_SPEAK_END:
        f->last_activity_ms = now;
        if (f->current == DSK_EMO_SPEAKING) ch = back_to_rest(f, now, "speak end");
        break;

    case DSK_EV_ALERT:
        f->last_activity_ms = now;
        f->alert_active = true;
        ch = enter(f, DSK_EMO_ALERT, hold, now, "alert");           /* hold 0 = until dismissed */
        f->alert_active = (hold == 0);
        break;

    case DSK_EV_DEADLINE_NEAR:
        f->last_activity_ms = now;
        ch = request(f, DSK_EMO_CONCERNED, hold ? hold : 6000, now, "deadline near");
        break;

    case DSK_EV_DEADLINE_NOW:
        f->last_activity_ms = now;
        f->alert_active = (hold == 0);
        ch = enter(f, DSK_EMO_DEADLINE_WARNING, hold, now, "deadline now");
        break;

    case DSK_EV_DISMISS:
        f->last_activity_ms = now;
        if (f->alert_active || f->current == DSK_EMO_ALERT || f->current == DSK_EMO_DEADLINE_WARNING)
            ch = back_to_rest(f, now, "dismissed");
        break;

    case DSK_EV_MAC_UP:
        f->mac_connected = true;
        f->last_activity_ms = now;
        if (!f->alert_active) {
            ch = enter(f, DSK_EMO_HAPPY, 2200, now, "mac up");
        }
        break;

    case DSK_EV_MAC_DOWN:
        f->mac_connected = false;
        f->last_activity_ms = now;
        if (!f->alert_active) {
            ch = enter(f, DSK_EMO_CONCERNED, 1500, now, "mac down");
            f->has_chain = false;
        }
        break;

    case DSK_EV_PAIRING:
        f->pairing = true;
        f->last_activity_ms = now;
        ch = enter(f, DSK_EMO_PAIRING, 0, now, "pairing");
        break;

    case DSK_EV_PAIRED:
        f->pairing = false;
        f->last_activity_ms = now;
        f->hold_ms = 1; f->entered_ms = now - 2;
        ch = request(f, DSK_EMO_VERY_HAPPY, 2500, now, "paired");
        break;

    case DSK_EV_SHAKE:
        f->last_activity_ms = now;
        if (!f->alert_active && pri_of(f->current) <= PRI_ACTIVITY)
            ch = enter(f, DSK_EMO_SURPRISED, 1200, now, "shake");
        break;

    case DSK_EV_SET_EMOTION:
        f->last_activity_ms = now;
        if (arg < DSK_EMO_COUNT) {
            f->hold_ms = 1; f->entered_ms = now - 2;
            ch = request(f, arg, hold, now, "explicit");
        }
        break;
    }
    return ch;
}

bool dsk_fsm_tick(dsk_fsm_t *f, uint32_t now)
{
    uint32_t in_state = now - f->entered_ms;

    /* 1. timed states expire */
    if (f->hold_ms && in_state >= f->hold_ms && !f->alert_active) {
        if (f->has_chain) {
            dsk_emotion_t e = f->chain_emo;
            uint32_t h = f->chain_hold_ms;
            return enter(f, e, h, now, "chain");
        }
        return back_to_rest(f, now, "hold expired");
    }

    /* 2. activity watchdog: never get stuck in LISTENING/THINKING/PROCESSING */
    if (!f->hold_ms && (f->current == DSK_EMO_LISTENING || f->current == DSK_EMO_THINKING ||
                        f->current == DSK_EMO_PROCESSING) && in_state >= f->cfg.activity_timeout_ms) {
        return enter(f, DSK_EMO_CONFUSED, 2500, now, "activity timeout");
    }

    /* 3. rest state follows connectivity (e.g. neutral -> disconnected) */
    dsk_emotion_t rest = dsk_fsm_rest_emotion(f);
    bool at_rest_family = (f->current == DSK_EMO_NEUTRAL || f->current == DSK_EMO_DISCONNECTED ||
                           f->current == DSK_EMO_PAIRING);
    if (at_rest_family && f->current != rest && !f->hold_ms) {
        return enter(f, rest, 0, now, "rest changed");
    }

    /* 4. idle ladder: waiting -> sleepy -> sleeping */
    if (!f->alert_active && !f->hold_ms && is_idle_state(f->current)) {
        uint32_t idle = now - f->last_activity_ms;
        dsk_emotion_t want = f->current;
        if (idle >= f->cfg.idle_sleeping_ms) want = DSK_EMO_SLEEPING;
        else if (idle >= f->cfg.idle_sleepy_ms) want = DSK_EMO_SLEEPY;
        else if (idle >= f->cfg.idle_waiting_ms && f->mac_connected && !f->pairing) want = DSK_EMO_WAITING;
        else if (idle >= f->cfg.disconnected_idle_ms && !f->mac_connected && !f->pairing) want = DSK_EMO_TIRED;
        if (want != f->current && !f->pairing) {
            /* never go back "up" the ladder by itself */
            bool downward = (want == DSK_EMO_SLEEPING) ||
                            (want == DSK_EMO_SLEEPY && f->current != DSK_EMO_SLEEPING) ||
                            (want == DSK_EMO_WAITING && (f->current == DSK_EMO_NEUTRAL || f->current == DSK_EMO_CALM)) ||
                            (want == DSK_EMO_TIRED && f->current == DSK_EMO_DISCONNECTED);
            if (downward) return enter(f, want, 0, now, "idle ladder");
        }
    }
    return false;
}
