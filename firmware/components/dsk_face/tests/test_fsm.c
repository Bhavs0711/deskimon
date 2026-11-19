/* Host-side test of the emotion state machine.  Build & run:
 *   gcc -Wall -I../include test_fsm.c ../dsk_fsm.c ../dsk_emotion.c -o test_fsm && ./test_fsm
 */
#include <stdio.h>
#include <string.h>
#include "dsk_fsm.h"

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails++; } else printf("ok   : %s\n", msg); } while (0)
#define EXPECT(f, e, msg) CHECK((f).current == (e), msg)

int main(void)
{
    dsk_fsm_t f; uint32_t t = 1000;
    dsk_fsm_init(&f, t);
    dsk_fsm_post(&f, DSK_EV_BOOT, DSK_EMO_COUNT, 0, t);
    EXPECT(f, DSK_EMO_DISCONNECTED, "boot without Mac -> disconnected");

    dsk_fsm_post(&f, DSK_EV_MAC_UP, DSK_EMO_COUNT, 0, t);
    EXPECT(f, DSK_EMO_HAPPY, "Mac connects -> happy");
    t += 2300; dsk_fsm_tick(&f, t);
    EXPECT(f, DSK_EMO_NEUTRAL, "happy expires -> neutral");

    dsk_fsm_post(&f, DSK_EV_LISTEN_START, DSK_EMO_COUNT, 0, t);
    EXPECT(f, DSK_EMO_LISTENING, "user speaks -> listening");
    dsk_fsm_post(&f, DSK_EV_CMD_SENT, DSK_EMO_COUNT, 0, t);
    EXPECT(f, DSK_EMO_THINKING, "command sent -> thinking");
    dsk_fsm_post(&f, DSK_EV_PROCESSING, DSK_EMO_COUNT, 0, t);
    EXPECT(f, DSK_EMO_PROCESSING, "processing");
    dsk_fsm_post(&f, DSK_EV_RESULT_OK, DSK_EMO_COUNT, 0, t);
    EXPECT(f, DSK_EMO_SUCCESS, "event created -> success");
    t += 2700; dsk_fsm_tick(&f, t);
    EXPECT(f, DSK_EMO_NEUTRAL, "success expires -> neutral");

    dsk_fsm_post(&f, DSK_EV_CMD_SENT, DSK_EMO_COUNT, 0, t);
    dsk_fsm_post(&f, DSK_EV_RESULT_ERR, DSK_EMO_COUNT, 0, t);
    EXPECT(f, DSK_EMO_ERROR, "failure -> error");
    t += 3300; dsk_fsm_tick(&f, t);
    EXPECT(f, DSK_EMO_CONFUSED, "error chains to confused");
    t += 2300; dsk_fsm_tick(&f, t);
    EXPECT(f, DSK_EMO_NEUTRAL, "confused expires -> neutral");

    dsk_fsm_post(&f, DSK_EV_TASK_DONE, DSK_EMO_COUNT, 0, t);
    EXPECT(f, DSK_EMO_PROUD, "task done -> proud");
    t += 1900; dsk_fsm_tick(&f, t);
    EXPECT(f, DSK_EMO_CELEBRATING, "proud chains to celebrating");
    t += 2900; dsk_fsm_tick(&f, t);
    EXPECT(f, DSK_EMO_NEUTRAL, "celebrating expires -> neutral");

    dsk_fsm_post(&f, DSK_EV_DEADLINE_NEAR, DSK_EMO_COUNT, 0, t);
    EXPECT(f, DSK_EMO_CONCERNED, "deadline approaching -> concerned");
    t += 6100; dsk_fsm_tick(&f, t);

    dsk_fsm_post(&f, DSK_EV_ALERT, DSK_EMO_COUNT, 0, t);
    EXPECT(f, DSK_EMO_ALERT, "alarm -> alert");
    dsk_fsm_post(&f, DSK_EV_RESULT_OK, DSK_EMO_COUNT, 0, t + 10);
    EXPECT(f, DSK_EMO_ALERT, "alert is not preempted by a result");
    t += 60000; dsk_fsm_tick(&f, t);
    EXPECT(f, DSK_EMO_ALERT, "alert persists until dismissed");
    dsk_fsm_post(&f, DSK_EV_DISMISS, DSK_EMO_COUNT, 0, t);
    EXPECT(f, DSK_EMO_NEUTRAL, "dismiss -> neutral");

    /* idle ladder */
    t += 1000; dsk_fsm_post(&f, DSK_EV_TOUCH, DSK_EMO_COUNT, 0, t);
    uint32_t t0 = t;
    t = t0 + 21000; dsk_fsm_tick(&f, t);
    EXPECT(f, DSK_EMO_WAITING, "20 s idle -> waiting");
    t = t0 + 91000; dsk_fsm_tick(&f, t);
    EXPECT(f, DSK_EMO_SLEEPY, "90 s idle -> sleepy");
    t = t0 + 181000; dsk_fsm_tick(&f, t);
    EXPECT(f, DSK_EMO_SLEEPING, "180 s idle -> sleeping");
    dsk_fsm_post(&f, DSK_EV_TOUCH, DSK_EMO_COUNT, 0, t);
    EXPECT(f, DSK_EMO_ALERT, "touch while sleeping -> alert (wake)");
    t += 800; dsk_fsm_tick(&f, t);
    EXPECT(f, DSK_EMO_NEUTRAL, "wake alert expires -> neutral");

    /* disconnect handling */
    dsk_fsm_post(&f, DSK_EV_MAC_DOWN, DSK_EMO_COUNT, 0, t);
    EXPECT(f, DSK_EMO_CONCERNED, "Mac lost -> concerned");
    t += 1600; dsk_fsm_tick(&f, t);
    EXPECT(f, DSK_EMO_DISCONNECTED, "then disconnected");

    /* pairing */
    dsk_fsm_post(&f, DSK_EV_PAIRING, DSK_EMO_COUNT, 0, t);
    EXPECT(f, DSK_EMO_PAIRING, "pairing mode");
    dsk_fsm_post(&f, DSK_EV_PAIRED, DSK_EMO_COUNT, 0, t);
    EXPECT(f, DSK_EMO_VERY_HAPPY, "paired -> very happy");

    /* watchdog */
    dsk_fsm_post(&f, DSK_EV_MAC_UP, DSK_EMO_COUNT, 0, t);
    t += 3000; dsk_fsm_tick(&f, t);
    dsk_fsm_post(&f, DSK_EV_CMD_SENT, DSK_EMO_COUNT, 0, t);
    t += 46000; dsk_fsm_tick(&f, t);
    EXPECT(f, DSK_EMO_CONFUSED, "no reply in 45 s -> confused (timeout)");

    /* names */
    dsk_emotion_t e;
    CHECK(dsk_emotion_from_name("VERY_HAPPY", &e) && e == DSK_EMO_VERY_HAPPY, "name lookup (case-insensitive)");
    CHECK(dsk_emotion_from_name("WOW", &e) && e == DSK_EMO_SURPRISED, "legacy alias WOW");
    CHECK(!dsk_emotion_from_name("nonsense", &e), "unknown name rejected");
    CHECK(DSK_EMO_COUNT >= 24, "at least 24 emotions");
    printf("%s (%d states)\n", fails ? "FAILED" : "ALL PASSED", (int)DSK_EMO_COUNT);
    return fails ? 1 : 0;
}
