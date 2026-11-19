/**
 * SDL2 Display and Input Driver for LVGL 8.x
 * Adapted for Deskimon simulator
 */

#include "sdl.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

/* Forward declaration for keyboard handler in main.c */
extern void handle_keyboard(SDL_Keycode key);

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static int32_t hor_res;
static int32_t ver_res;
static bool mouse_pressed = false;
static int16_t mouse_x = 0;
static int16_t mouse_y = 0;

void sdl_init(void)
{
    SDL_Init(SDL_INIT_VIDEO);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
}

static void sdl_display_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    /* Calculate area dimensions */
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    
    /* Allocate buffer for RGB888 conversion */
    uint32_t *pixels = malloc(w * h * sizeof(uint32_t));
    if (!pixels) {
        lv_disp_flush_ready(disp_drv);
        return;
    }
    
    /* Convert RGB565 to ARGB8888 for SDL */
    for(int32_t y = 0; y < h; y++) {
        for(int32_t x = 0; x < w; x++) {
            lv_color_t c = color_p[y * w + x];
            uint8_t r = LV_COLOR_GET_R(c) << 3;  /* 5-bit to 8-bit */
            uint8_t g = LV_COLOR_GET_G(c) << 2;  /* 6-bit to 8-bit */
            uint8_t b = LV_COLOR_GET_B(c) << 3;  /* 5-bit to 8-bit */
            pixels[y * w + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        }
    }
    
    /* Update texture with converted pixels */
    SDL_Rect rect;
    rect.x = area->x1;
    rect.y = area->y1;
    rect.w = w;
    rect.h = h;
    
    SDL_UpdateTexture(texture, &rect, pixels, w * sizeof(uint32_t));
    free(pixels);
    
    /* Mark flush complete */
    lv_disp_flush_ready(disp_drv);
}

static void sdl_display_render(void)
{
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

static void sdl_mouse_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    SDL_Event event;
    while(SDL_PollEvent(&event)) {
        switch(event.type) {
            case SDL_MOUSEBUTTONDOWN:
                if(event.button.button == SDL_BUTTON_LEFT) {
                    mouse_pressed = true;
                    mouse_x = event.button.x;
                    mouse_y = event.button.y;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if(event.button.button == SDL_BUTTON_LEFT) {
                    mouse_pressed = false;
                }
                break;
            case SDL_MOUSEMOTION:
                if(mouse_pressed) {
                    mouse_x = event.motion.x;
                    mouse_y = event.motion.y;
                }
                break;
            case SDL_KEYDOWN:
                /* Forward keyboard events to main.c handler */
                handle_keyboard(event.key.keysym.sym);
                break;
            case SDL_QUIT:
                exit(0);
                break;
        }
    }
    
    data->point.x = mouse_x;
    data->point.y = mouse_y;
    data->state = mouse_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    
    sdl_display_render();
}

void sdl_disp_drv_init(lv_disp_drv_t *disp_drv, int32_t h_res, int32_t v_res)
{
    hor_res = h_res;
    ver_res = v_res;
    
    window = SDL_CreateWindow("Deskimon Simulator",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              hor_res, ver_res,
                              SDL_WINDOW_SHOWN);
    
    if (!window) {
        printf("Failed to create SDL window: %s\n", SDL_GetError());
        exit(1);
    }
    
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        printf("Failed to create SDL renderer: %s\n", SDL_GetError());
        exit(1);
    }
    
    texture = SDL_CreateTexture(renderer,
                               SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_STREAMING,
                               hor_res, ver_res);
    
    if (!texture) {
        printf("Failed to create SDL texture: %s\n", SDL_GetError());
        exit(1);
    }
    
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    
    lv_disp_draw_buf_t *disp_buf = malloc(sizeof(lv_disp_draw_buf_t));
    uint32_t buf_size = hor_res * 40;  /* 40 lines buffer */
    lv_color_t *buf1 = malloc(buf_size * sizeof(lv_color_t));
    lv_color_t *buf2 = malloc(buf_size * sizeof(lv_color_t));
    
    if (!buf1 || !buf2) {
        printf("Failed to allocate LVGL buffers\n");
        exit(1);
    }
    
    lv_disp_draw_buf_init(disp_buf, buf1, buf2, buf_size);
    
    lv_disp_drv_init(disp_drv);
    disp_drv->draw_buf = disp_buf;
    disp_drv->flush_cb = sdl_display_flush;
    disp_drv->hor_res = hor_res;
    disp_drv->ver_res = ver_res;
}

void sdl_indev_drv_init(lv_indev_drv_t *indev_drv)
{
    lv_indev_drv_init(indev_drv);
    indev_drv->type = LV_INDEV_TYPE_POINTER;
    indev_drv->read_cb = sdl_mouse_read;
}
