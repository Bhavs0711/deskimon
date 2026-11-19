#include "audio_test.h"
#include "voice_bridge.h"

OPERATE_RET audio_test_play_beep(void)
{
    return voice_bridge_play_beep();
}
