#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 30 distinct visual states. Order matters: it is the index into the pose table. */
typedef enum {
    DSK_EMO_NEUTRAL = 0,
    DSK_EMO_HAPPY,
    DSK_EMO_VERY_HAPPY,
    DSK_EMO_EXCITED,
    DSK_EMO_FOCUSED,
    DSK_EMO_THINKING,
    DSK_EMO_LISTENING,
    DSK_EMO_SPEAKING,
    DSK_EMO_PROCESSING,
    DSK_EMO_CONFUSED,
    DSK_EMO_CONCERNED,
    DSK_EMO_WORRIED,
    DSK_EMO_TIRED,
    DSK_EMO_SLEEPY,
    DSK_EMO_SLEEPING,
    DSK_EMO_SURPRISED,
    DSK_EMO_PROUD,
    DSK_EMO_CELEBRATING,
    DSK_EMO_CURIOUS,
    DSK_EMO_WAITING,
    DSK_EMO_ALERT,
    DSK_EMO_DEADLINE_WARNING,
    DSK_EMO_SUCCESS,
    DSK_EMO_ERROR,
    DSK_EMO_DISAPPOINTED,
    DSK_EMO_MOTIVATING,
    DSK_EMO_BUSY,
    DSK_EMO_CALM,
    DSK_EMO_DISCONNECTED,
    DSK_EMO_PAIRING,
    DSK_EMO_COUNT
} dsk_emotion_t;

const char *dsk_emotion_name(dsk_emotion_t e);

/** Case-insensitive lookup. Also accepts the legacy names of the previous firmware
 *  (IDLE, SAD, WOW, ANGRY, DIZZY, ...). Returns false if unknown. */
bool dsk_emotion_from_name(const char *name, dsk_emotion_t *out);

#ifdef __cplusplus
}
#endif
