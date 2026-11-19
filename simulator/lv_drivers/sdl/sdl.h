/**
 * SDL2 Display and Input Driver for LVGL 8.x
 */

#ifndef LV_SDL_H
#define LV_SDL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

void sdl_init(void);
void sdl_disp_drv_init(lv_disp_drv_t *disp_drv, int32_t hor_res, int32_t ver_res);
void sdl_indev_drv_init(lv_indev_drv_t *indev_drv);

#ifdef __cplusplus
}
#endif

#endif /* LV_SDL_H */
