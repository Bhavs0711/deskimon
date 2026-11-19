#include "tal_api.h"
#include "tkl_output.h"

#include "board_com_api.h"
#include "lv_vendor.h"

#include "ble_agent.h"
#include "eye_scene.h"
#include "motion_qmi8658.h"
#include "voice_bridge.h"

static void user_main(void)
{
    OPERATE_RET rt = OPRT_OK;

    tal_log_init(TAL_LOG_LEVEL_DEBUG, 4096, (TAL_LOG_OUTPUT_CB)tkl_log_output);

    PR_NOTICE("Application information:");
    PR_NOTICE("Project name:        %s", PROJECT_NAME);
    PR_NOTICE("App version:         %s", PROJECT_VERSION);
    PR_NOTICE("Compile time:        %s", __DATE__);
    PR_NOTICE("TuyaOpen version:    %s", OPEN_VERSION);
    PR_NOTICE("TuyaOpen commit-id:  %s", OPEN_COMMIT);
    PR_NOTICE("Platform chip:       %s", PLATFORM_CHIP);
    PR_NOTICE("Platform board:      %s", PLATFORM_BOARD);
    PR_NOTICE("Platform commit-id:  %s", PLATFORM_COMMIT);

    tal_kv_init(&(tal_kv_cfg_t){
        .seed = "t5e1eyeagentseed",
        .key = "t5e1eyeagentkey1",
    });
    tal_sw_timer_init();
    tal_workq_init();

    TUYA_CALL_ERR_LOG(board_register_hardware());
    TUYA_CALL_ERR_LOG(voice_bridge_init());

    lv_vendor_init(DISPLAY_NAME);
    TUYA_CALL_ERR_LOG(eye_scene_init());
    lv_vendor_start(5, 1024 * 8);

    TUYA_CALL_ERR_LOG(motion_qmi8658_start());
    TUYA_CALL_ERR_LOG(ble_agent_init());
}

#if OPERATING_SYSTEM == SYSTEM_LINUX
void main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    user_main();

    while (1) {
        tal_system_sleep(500);
    }
}
#else
static THREAD_HANDLE ty_app_thread = NULL;

static void tuya_app_thread(void *arg)
{
    (void)arg;
    user_main();

    tal_thread_delete(ty_app_thread);
    ty_app_thread = NULL;
}

void tuya_app_main(void)
{
    THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth = 1024 * 8;
    thrd_param.priority = THREAD_PRIO_1;
    thrd_param.thrdname = "tuya_app_main";

    tal_thread_create_and_start(&ty_app_thread, NULL, NULL, tuya_app_thread, NULL, &thrd_param);
}
#endif
