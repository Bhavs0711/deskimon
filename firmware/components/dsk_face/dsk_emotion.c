#include <ctype.h>
#include <string.h>
#include "dsk_emotion.h"

static const char *const k_names[DSK_EMO_COUNT] = {
    "neutral", "happy", "very_happy", "excited", "focused", "thinking", "listening", "speaking",
    "processing", "confused", "concerned", "worried", "tired", "sleepy", "sleeping", "surprised",
    "proud", "celebrating", "curious", "waiting", "alert", "deadline_warning", "success", "error",
    "disappointed", "motivating", "busy", "calm", "disconnected", "pairing",
};

typedef struct { const char *alias; dsk_emotion_t emo; } alias_t;
static const alias_t k_alias[] = {
    {"idle", DSK_EMO_NEUTRAL},       {"sad", DSK_EMO_DISAPPOINTED},   {"wow", DSK_EMO_SURPRISED},
    {"angry", DSK_EMO_ALERT},        {"angry_soft", DSK_EMO_FOCUSED}, {"angry_hard", DSK_EMO_ALERT},
    {"dizzy", DSK_EMO_CONFUSED},     {"delighted", DSK_EMO_VERY_HAPPY}, {"joy", DSK_EMO_VERY_HAPPY},
    {"listen", DSK_EMO_LISTENING},   {"think", DSK_EMO_THINKING},     {"speak", DSK_EMO_SPEAKING},
    {"bored", DSK_EMO_TIRED},        {"mail", DSK_EMO_CURIOUS},       {"call", DSK_EMO_ALERT},
    {"warning", DSK_EMO_DEADLINE_WARNING}, {"deadline", DSK_EMO_DEADLINE_WARNING},
    {"celebrate", DSK_EMO_CELEBRATING},    {"sleep", DSK_EMO_SLEEPING},
    {"wait", DSK_EMO_WAITING},       {"offline", DSK_EMO_DISCONNECTED}, {"done", DSK_EMO_SUCCESS},
    {"fail", DSK_EMO_ERROR},         {"motivate", DSK_EMO_MOTIVATING},
};

const char *dsk_emotion_name(dsk_emotion_t e)
{
    return (e >= 0 && e < DSK_EMO_COUNT) ? k_names[e] : "neutral";
}

bool dsk_emotion_from_name(const char *name, dsk_emotion_t *out)
{
    if (!name || !out) return false;
    char buf[24];
    size_t n = 0;
    for (; name[n] && n < sizeof(buf) - 1; n++) buf[n] = (char)tolower((unsigned char)name[n]);
    buf[n] = 0;
    if (name[n]) return false;            /* too long to be a name */
    for (int i = 0; i < DSK_EMO_COUNT; i++)
        if (!strcmp(buf, k_names[i])) { *out = (dsk_emotion_t)i; return true; }
    for (size_t i = 0; i < sizeof(k_alias) / sizeof(k_alias[0]); i++)
        if (!strcmp(buf, k_alias[i].alias)) { *out = k_alias[i].emo; return true; }
    return false;
}
