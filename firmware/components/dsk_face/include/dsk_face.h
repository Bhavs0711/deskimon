#pragma once
#include "esp_err.h"
#include "lvgl.h"
#include "dsk_emotion.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Build the face on the active screen. Call once, after dsk_board_init(). */
esp_err_t dsk_face_init(void);

/** Switch the displayed emotion (thread-safe; takes the LVGL lock itself). */
void dsk_face_set_emotion(dsk_emotion_t e);
dsk_emotion_t dsk_face_get_emotion(void);

/** IMU tilt in -1..1 steers the pupils (optional). */
void dsk_face_set_tilt(float tilt_x, float tilt_y);

/** Root object of the face (children: eyes, brows, mouth, effects). */
lv_obj_t *dsk_face_root(void);

#ifdef __cplusplus
}
#endif
