#include <stdio.h>
#include <SDL.h>
#include "lvgl.h"
#include "dsk_face.h"
#include "dsk_emotion.h"

#define SCREEN_W 360
#define SCREEN_H 360

static lv_color_t draw_buffer[SCREEN_W * 40];

static void flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *pixels)
{
    SDL_Rect rect = {
        area->x1, area->y1,
        area->x2 - area->x1 + 1,
        area->y2 - area->y1 + 1
    };

    SDL_UpdateTexture(
        (SDL_Texture *)disp->user_data,
        &rect, pixels, rect.w * sizeof(lv_color_t)
    );

    SDL_RenderClear(SDL_GetRenderer(SDL_GetWindowFromID(1)));
    SDL_RenderCopy(
        SDL_GetRenderer(SDL_GetWindowFromID(1)),
        (SDL_Texture *)disp->user_data, NULL, NULL
    );
    SDL_RenderPresent(SDL_GetRenderer(SDL_GetWindowFromID(1)));

    lv_disp_flush_ready(disp);
}

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "Deskimon - LVGL PC Simulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        720, 720, SDL_WINDOW_SHOWN
    );
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);

    SDL_Texture *texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H
    );

    if (!window || !renderer || !texture) {
        fprintf(stderr, "SDL setup failed: %s\n", SDL_GetError());
        return 1;
    }

    lv_init();

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, draw_buffer, NULL, SCREEN_W * 40);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_W;
    disp_drv.ver_res = SCREEN_H;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = flush_cb;
    disp_drv.user_data = texture;
    lv_disp_drv_register(&disp_drv);

    if (dsk_face_init() != ESP_OK) {
        fprintf(stderr, "Deskimon face initialization failed\n");
        return 1;
    }

    int emotion = DSK_EMO_DISCONNECTED;
    dsk_face_set_emotion((dsk_emotion_t)emotion);

    printf("\nDeskimon LVGL Simulator\n");
    printf("Left/Right: change emotion | ESC: quit\n\n");

    int running = 1;
    Uint32 previous = SDL_GetTicks();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = 0;

            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = 0;
                } else if (event.key.keysym.sym == SDLK_RIGHT) {
                    emotion = (emotion + 1) % DSK_EMO_COUNT;
                    dsk_face_set_emotion((dsk_emotion_t)emotion);
                    printf("Emotion: %s\n", dsk_emotion_name((dsk_emotion_t)emotion));
                } else if (event.key.keysym.sym == SDLK_LEFT) {
                    emotion = (emotion - 1 + DSK_EMO_COUNT) % DSK_EMO_COUNT;
                    dsk_face_set_emotion((dsk_emotion_t)emotion);
                    printf("Emotion: %s\n", dsk_emotion_name((dsk_emotion_t)emotion));
                }
            }
        }

        Uint32 now = SDL_GetTicks();
        lv_tick_inc(now - previous);
        previous = now;
        lv_timer_handler();
        SDL_Delay(5);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
