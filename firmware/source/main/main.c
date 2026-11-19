#include "esp_log.h"
#include "dsk_board.h"
#include "dsk_face.h"
#include "dsk_overlay.h"
#include "dsk_app.h"

/* Speaker hardware test — compiled only when CONFIG_DSK_SPEAKER_TEST=y */
#ifdef CONFIG_DSK_SPEAKER_TEST
extern void dsk_speaker_test_run(void);
#endif

void app_main(void)
{
    ESP_ERROR_CHECK(dsk_board_init());
    ESP_ERROR_CHECK(dsk_face_init());
    ESP_ERROR_CHECK(dsk_overlay_init());
    dsk_board_log_memory("boot");

#ifdef CONFIG_DSK_SPEAKER_TEST
    dsk_speaker_test_run();
#endif

    ESP_ERROR_CHECK(dsk_app_start());
}
