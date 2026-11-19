#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "tal_api.h"
#include "tal_bluetooth.h"
#include "tkl_output.h"

#include "ble_agent.h"
#include "eye_scene.h"

static bool sg_notify_enabled = false;
static TAL_BLE_PEER_INFO_T sg_ble_peer;

static uint8_t sg_adv_data[] = {
    0x02, 0x01, 0x06,
    0x03, 0x03, 0xFD, 0xFD,
};

static uint8_t sg_scan_rsp[] = {
    0x0A, 0x09, 'T', '5', 'E', '1', '-', 'E', 'Y', 'E', 'S',
};

static void __ble_reply(const char *reply)
{
    TAL_BLE_DATA_T pkt = {0};

    if (NULL == reply || '\0' == reply[0]) {
        return;
    }

    pkt.len = (uint16_t)strlen(reply);
    pkt.p_data = (uint8_t *)reply;
    tal_ble_server_common_read_update(&pkt);

    if (sg_notify_enabled) {
        tal_ble_server_common_send(&pkt);
    }
}

static void __ble_event_cb(TAL_BLE_EVT_PARAMS_T *evt)
{
    char reply[160] = {0};

    PR_DEBUG("ble event: %d", evt->type);

    switch (evt->type) {
    case TAL_BLE_STACK_INIT: {
        TAL_BLE_DATA_T adv = {
            .len = sizeof(sg_adv_data),
            .p_data = sg_adv_data,
        };
        TAL_BLE_DATA_T rsp = {
            .len = sizeof(sg_scan_rsp),
            .p_data = sg_scan_rsp,
        };

        if (0 == evt->ble_event.init) {
            tal_system_sleep(1000);
            tal_ble_advertising_data_set(&adv, &rsp);
            tal_ble_advertising_start(TUYAOS_BLE_DEFAULT_ADV_PARAM);
            PR_NOTICE("BLE advertising as T5E1-EYES");
        }
        break;
    }

    case TAL_BLE_EVT_PERIPHERAL_CONNECT:
        if (0 == evt->ble_event.connect.result) {
            memcpy(&sg_ble_peer, &evt->ble_event.connect.peer, sizeof(sg_ble_peer));
            __ble_reply("READY T5E1-EYES");
        } else {
            memset(&sg_ble_peer, 0, sizeof(sg_ble_peer));
        }
        break;

    case TAL_BLE_EVT_DISCONNECT:
        sg_notify_enabled = false;
        memset(&sg_ble_peer, 0, sizeof(sg_ble_peer));
        tal_ble_advertising_start(TUYAOS_BLE_DEFAULT_ADV_PARAM);
        break;

    case TAL_BLE_EVT_SUBSCRIBE:
        sg_notify_enabled = (evt->ble_event.subscribe.cur_notify != 0);
        if (sg_notify_enabled) {
            __ble_reply("NOTIFY ON");
        }
        break;

    case TAL_BLE_EVT_WRITE_REQ: {
        char cmd[128] = {0};
        uint16_t copy_len = evt->ble_event.write_report.report.len;
        if (copy_len >= sizeof(cmd)) {
            copy_len = sizeof(cmd) - 1;
        }

        memcpy(cmd, evt->ble_event.write_report.report.p_data, copy_len);
        cmd[copy_len] = '\0';

        while (copy_len > 0 && isspace((unsigned char)cmd[copy_len - 1])) {
            cmd[copy_len - 1] = '\0';
            copy_len--;
        }

        if (0 == strcmp(cmd, "?")) {
            snprintf(reply, sizeof(reply), "OK HELP");
        } else {
            eye_scene_process_command(cmd, reply, sizeof(reply));
        }

        __ble_reply(reply);
        PR_NOTICE("BLE command: %s -> %s", cmd, reply);
        break;
    }

    default:
        break;
    }
}

OPERATE_RET ble_agent_init(void)
{
    return tal_ble_bt_init(TAL_BLE_ROLE_PERIPERAL, __ble_event_cb);
}
