#include <stdlib.h>
#include <string.h>

#include "tal_api.h"
#include "tkl_i2c.h"
#include "tkl_output.h"
#include "tkl_pinmux.h"

#include "eye_scene.h"
#include "motion_qmi8658.h"

#define QMI8658_I2C_PORT         TUYA_I2C_NUM_0
#define QMI8658_SCL_PIN          TUYA_GPIO_NUM_20
#define QMI8658_SDA_PIN          TUYA_GPIO_NUM_21
#define QMI8658_ADDR_LOW         0x6A
#define QMI8658_ADDR_HIGH        0x6B

#define QMI8658_REG_WHOAMI       0x00
#define QMI8658_REG_CTRL1        0x02
#define QMI8658_REG_CTRL2        0x03
#define QMI8658_REG_CTRL3        0x04
#define QMI8658_REG_CTRL5        0x06
#define QMI8658_REG_CTRL7        0x08
#define QMI8658_REG_ACC_X_L      0x35

static THREAD_HANDLE sg_motion_thread = NULL;
static uint8_t sg_qmi_addr = 0;

static TUYA_IIC_BASE_CFG_T sg_i2c_cfg = {
    .role = TUYA_IIC_MODE_MASTER,
    .speed = TUYA_IIC_BUS_SPEED_400K,
    .addr_width = TUYA_IIC_ADDRESS_7BIT,
};

static OPERATE_RET __qmi_write(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return tkl_i2c_master_send(QMI8658_I2C_PORT, sg_qmi_addr, buf, sizeof(buf), FALSE);
}

static OPERATE_RET __qmi_read(uint8_t reg, uint8_t *buf, uint8_t len)
{
    OPERATE_RET rt = tkl_i2c_master_send(QMI8658_I2C_PORT, sg_qmi_addr, &reg, 1, FALSE);
    if (OPRT_OK != rt) {
        return rt;
    }

    return tkl_i2c_master_receive(QMI8658_I2C_PORT, sg_qmi_addr, buf, len, FALSE);
}

static bool __qmi_detect_addr(uint8_t addr)
{
    uint8_t whoami = 0;
    sg_qmi_addr = addr;

    if (OPRT_OK != __qmi_read(QMI8658_REG_WHOAMI, &whoami, 1)) {
        return false;
    }

    if (0x00 == whoami || 0xFF == whoami) {
        return false;
    }

    PR_NOTICE("QMI8658 detected at 0x%02X (WHOAMI=0x%02X)", addr, whoami);
    return true;
}

static OPERATE_RET __qmi_configure(void)
{
    OPERATE_RET rt = OPRT_OK;

    rt = __qmi_write(QMI8658_REG_CTRL1, 0x40);
    TUYA_CALL_ERR_RETURN(rt);

    rt = __qmi_write(QMI8658_REG_CTRL2, 0x23);
    TUYA_CALL_ERR_RETURN(rt);

    rt = __qmi_write(QMI8658_REG_CTRL3, 0x43);
    TUYA_CALL_ERR_RETURN(rt);

    rt = __qmi_write(QMI8658_REG_CTRL5, 0x11);
    TUYA_CALL_ERR_RETURN(rt);

    rt = __qmi_write(QMI8658_REG_CTRL7, 0x03);
    TUYA_CALL_ERR_RETURN(rt);

    return OPRT_OK;
}

static void __motion_thread(void *arg)
{
    int16_t last_ax = 0;
    int16_t last_ay = 0;
    int16_t last_az = 0;
    EYE_MOTION_STATE_T motion = {0};

    (void)arg;

    tkl_io_pinmux_config(QMI8658_SCL_PIN, TUYA_IIC0_SCL);
    tkl_io_pinmux_config(QMI8658_SDA_PIN, TUYA_IIC0_SDA);

    if (OPRT_OK != tkl_i2c_init(QMI8658_I2C_PORT, &sg_i2c_cfg)) {
        PR_NOTICE("I2C init returned non-zero, continuing with shared board bus");
    }

    if (!__qmi_detect_addr(QMI8658_ADDR_HIGH) && !__qmi_detect_addr(QMI8658_ADDR_LOW)) {
        PR_ERR("QMI8658 not detected on the shared touch/RTC bus");
        eye_scene_set_motion_state(&motion);
        goto __exit;
    }

    if (OPRT_OK != __qmi_configure()) {
        PR_ERR("QMI8658 configuration failed");
        goto __exit;
    }

    motion.present = true;

    while (1) {
        uint8_t raw[12] = {0};
        int16_t ax = 0;
        int16_t ay = 0;
        int16_t az = 0;
        int16_t gx = 0;
        int16_t gy = 0;
        int16_t gz = 0;
        float accel_delta = 0.0f;
        float gyro_mag = 0.0f;

        if (OPRT_OK == __qmi_read(QMI8658_REG_ACC_X_L, raw, sizeof(raw))) {
            ax = (int16_t)((raw[1] << 8) | raw[0]);
            ay = (int16_t)((raw[3] << 8) | raw[2]);
            az = (int16_t)((raw[5] << 8) | raw[4]);
            gx = (int16_t)((raw[7] << 8) | raw[6]);
            gy = (int16_t)((raw[9] << 8) | raw[8]);
            gz = (int16_t)((raw[11] << 8) | raw[10]);

            accel_delta = (float)(abs(ax - last_ax) + abs(ay - last_ay) + abs(az - last_az)) / 8192.0f;
            gyro_mag = (float)(abs(gx) + abs(gy) + abs(gz)) / 4096.0f;

            motion.tilt_x = (float)ax / 16384.0f;
            motion.tilt_y = (float)ay / 16384.0f;
            motion.activity = accel_delta + gyro_mag;
            motion.gyro = gyro_mag;

            eye_scene_set_motion_state(&motion);

            last_ax = ax;
            last_ay = ay;
            last_az = az;
        }

        tal_system_sleep(40);
    }

__exit:
    tal_thread_delete(sg_motion_thread);
    sg_motion_thread = NULL;
}

OPERATE_RET motion_qmi8658_start(void)
{
    THREAD_CFG_T thrd_param = {0};

    thrd_param.stackDepth = 1024 * 4;
    thrd_param.priority = THREAD_PRIO_2;
    thrd_param.thrdname = "motion_qmi8658";

    return tal_thread_create_and_start(&sg_motion_thread, NULL, NULL, __motion_thread, NULL, &thrd_param);
}
