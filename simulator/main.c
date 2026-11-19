/**
 * Deskimon PC Simulator Main Entry Point
 * Runs the actual Deskimon UI on desktop using SDL2
 * 
 * Keyboard Controls:
 * - Number keys 0-9: Set emotions (0=NEUTRAL, 1=HAPPY, 2=VERY_HAPPY, etc.)
 * - Arrow Left/Right: Previous/Next emotion
 * - Space: Show "Task Completed" overlay
 * - T: Toggle auto-cycle emotions (default: OFF)
 * - C: Show "Calendar Event" overlay
 * - M: Show "Email" overlay
 * - P: Show "Processing" overlay
 * - H: Show help
 * - Ctrl+C: Exit
 * 
 * HTTP API (http://localhost:8080):
 * - POST /emotion {"emotion": "happy"}
 * - POST /overlay {"title": "Task Done", "subtitle": "Completed", "kind": "status"}
 * - GET /status
 */

#include "lvgl.h"
#include "lv_drivers/sdl/sdl.h"
#include "hal_sim.h"
#include "http_control.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <stdbool.h>
#include <pthread.h>

/* Deskimon UI components */
extern esp_err_t dsk_face_init(void);
extern esp_err_t dsk_overlay_init(void);
extern void dsk_face_set_emotion(dsk_emotion_t e);
extern lv_obj_t *dsk_face_root(void);
extern void dsk_overlay_show(dsk_overlay_kind_t kind, const char *title, const char *subtitle, uint32_t duration_ms);
extern void dsk_overlay_hide(void);
extern const char *dsk_emotion_name(dsk_emotion_t e);

/* Global display and input device */
static lv_disp_t *disp;
static lv_indev_t *indev;
static bool running = true;
static bool auto_cycle_emotions = false;  /* OFF by default */
static dsk_emotion_t current_emotion = DSK_EMO_NEUTRAL;
static pthread_mutex_t emotion_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Signal handler for clean exit */
static void signal_handler(int sig)
{
    (void)sig;
    running = false;
    http_control_stop();
}

/* Get time in milliseconds */
static uint32_t get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

/* Set emotion and print info */
static void set_emotion(dsk_emotion_t emotion)
{
    if (emotion >= 0 && emotion < DSK_EMO_COUNT) {
        pthread_mutex_lock(&emotion_mutex);
        current_emotion = emotion;
        dsk_face_set_emotion(emotion);
        pthread_mutex_unlock(&emotion_mutex);
        printf("[Control] Emotion set to: %d (%s)\n", emotion, dsk_emotion_name(emotion));
        fflush(stdout);
    }
}

/* Show overlay with message */
static void show_overlay(dsk_overlay_kind_t kind, const char *title, const char *subtitle)
{
    dsk_overlay_show(kind, title, subtitle, 3000);  /* 3 seconds */
    printf("[Control] Overlay shown: %s - %s\n", title, subtitle);
    fflush(stdout);
}

/* External wrappers for HTTP server (thread-safe) */
void set_emotion_external(dsk_emotion_t emotion)
{
    set_emotion(emotion);
}

void show_overlay_external(dsk_overlay_kind_t kind, const char *title, const char *subtitle)
{
    show_overlay(kind, title, subtitle);
}

dsk_emotion_t get_current_emotion(void)
{
    dsk_emotion_t e;
    pthread_mutex_lock(&emotion_mutex);
    e = current_emotion;
    pthread_mutex_unlock(&emotion_mutex);
    return e;
}

/* Handle keyboard input for manual control (called from SDL driver) */
void handle_keyboard(SDL_Keycode key)
{
    switch (key) {
        /* Number keys 0-9 for direct emotion selection */
        case SDLK_0: set_emotion(DSK_EMO_NEUTRAL); break;
        case SDLK_1: set_emotion(DSK_EMO_HAPPY); break;
        case SDLK_2: set_emotion(DSK_EMO_VERY_HAPPY); break;
        case SDLK_3: set_emotion(DSK_EMO_EXCITED); break;
        case SDLK_4: set_emotion(DSK_EMO_FOCUSED); break;
        case SDLK_5: set_emotion(DSK_EMO_THINKING); break;
        case SDLK_6: set_emotion(DSK_EMO_LISTENING); break;
        case SDLK_7: set_emotion(DSK_EMO_SPEAKING); break;
        case SDLK_8: set_emotion(DSK_EMO_PROCESSING); break;
        case SDLK_9: set_emotion(DSK_EMO_CONFUSED); break;
        
        /* Arrow keys for prev/next emotion */
        case SDLK_LEFT:
            set_emotion((current_emotion - 1 + DSK_EMO_COUNT) % DSK_EMO_COUNT);
            break;
        case SDLK_RIGHT:
            set_emotion((current_emotion + 1) % DSK_EMO_COUNT);
            break;
        
        /* Toggle auto-cycle */
        case SDLK_t:
            auto_cycle_emotions = !auto_cycle_emotions;
            printf("[Control] Auto-cycle emotions: %s\n", auto_cycle_emotions ? "ON" : "OFF");
            fflush(stdout);
            break;
        
        /* Overlay shortcuts */
        case SDLK_SPACE:
            show_overlay(DSK_OVL_STATUS, "Task Completed", "✓ Project review scheduled");
            set_emotion(DSK_EMO_SUCCESS);
            break;
        
        case SDLK_c:
            show_overlay(DSK_OVL_MEETING, "Calendar Event", "Tomorrow 2:00 PM");
            set_emotion(DSK_EMO_ALERT);
            break;
        
        case SDLK_m:
            show_overlay(DSK_OVL_EMAIL, "New Email", "From: Swift Agent");
            set_emotion(DSK_EMO_CURIOUS);
            break;
        
        case SDLK_p:
            show_overlay(DSK_OVL_PROCESSING, "Processing", "Working on your request...");
            set_emotion(DSK_EMO_PROCESSING);
            break;
        
        case SDLK_r:
            show_overlay(DSK_OVL_REMINDER, "Reminder", "Break time!");
            set_emotion(DSK_EMO_WAITING);
            break;
        
        case SDLK_a:
            show_overlay(DSK_OVL_ALARM, "Alert", "Meeting starts in 5 min");
            set_emotion(DSK_EMO_DEADLINE_WARNING);
            break;
        
        case SDLK_ESCAPE:
            dsk_overlay_hide();
            printf("[Control] Overlay hidden\n");
            fflush(stdout);
            break;
        
        /* Help */
        case SDLK_h:
            printf("\n========== KEYBOARD CONTROLS ==========\n");
            printf("Numbers 0-9: Set emotions directly\n");
            printf("  0=Neutral, 1=Happy, 2=Very Happy, 3=Excited\n");
            printf("  4=Focused, 5=Thinking, 6=Listening, 7=Speaking\n");
            printf("  8=Processing, 9=Confused\n");
            printf("Arrow Left/Right: Previous/Next emotion\n");
            printf("\nOverlays:\n");
            printf("  Space: Task Completed\n");
            printf("  C: Calendar Event\n");
            printf("  M: Email\n");
            printf("  P: Processing\n");
            printf("  R: Reminder\n");
            printf("  A: Alert\n");
            printf("  ESC: Hide overlay\n");
            printf("\nOther:\n");
            printf("  T: Toggle auto-cycle emotions\n");
            printf("  H: Show this help\n");
            printf("  Ctrl+C: Exit\n");
            printf("======================================\n\n");
            fflush(stdout);
            break;
    }
}

/* Initialize Deskimon UI */
static esp_err_t init_deskimon_ui(void)
{
    printf("[Simulator] Initializing Deskimon UI...\n");
    fflush(stdout);
    
    /* Initialize face (main UI) */
    printf("[Simulator] Calling dsk_face_init()...\n");
    fflush(stdout);
    esp_err_t ret = dsk_face_init();
    printf("[Simulator] dsk_face_init() returned: %d\n", ret);
    fflush(stdout);
    
    if (ret != ESP_OK) {
        printf("[Simulator] Failed to initialize face: %d\n", ret);
        return ret;
    }
    
    /* Initialize overlay system */
    printf("[Simulator] Calling dsk_overlay_init()...\n");
    fflush(stdout);
    ret = dsk_overlay_init();
    printf("[Simulator] dsk_overlay_init() returned: %d\n", ret);
    fflush(stdout);
    
    if (ret != ESP_OK) {
        printf("[Simulator] Failed to initialize overlay: %d\n", ret);
        return ret;
    }
    
    printf("[Simulator] Deskimon UI initialized successfully\n");
    fflush(stdout);
    return ESP_OK;
}

/* Cycle through emotions for demo (only if auto-cycle enabled) */
static void demo_emotions(void)
{
    if (!auto_cycle_emotions) return;
    
    static uint32_t last_change = 0;
    
    uint32_t now = lv_tick_get();
    if (now - last_change > 3000) {  /* Change every 3 seconds */
        current_emotion = (current_emotion + 1) % DSK_EMO_COUNT;
        dsk_face_set_emotion(current_emotion);
        printf("[Auto] Emotion changed to: %d (%s)\n", current_emotion, dsk_emotion_name(current_emotion));
        last_change = now;
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    printf("========================================\n");
    printf("  Deskimon PC Simulator\n");
    printf("  Display: %dx%d\n", DSK_LCD_W, DSK_LCD_H);
    printf("  LVGL Version: %d.%d.%d\n", 
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    printf("========================================\n\n");
    
    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Initialize SDL2 */
    printf("[Simulator] Initializing SDL2...\n");
    sdl_init();
    
    /* Initialize LVGL */
    printf("[Simulator] Initializing LVGL...\n");
    lv_init();
    
    /* Setup LVGL display driver */
    lv_disp_drv_t disp_drv;
    sdl_disp_drv_init(&disp_drv, DSK_LCD_W, DSK_LCD_H);
    disp = lv_disp_drv_register(&disp_drv);
    printf("[Simulator] Display registered: %dx%d\n", DSK_LCD_W, DSK_LCD_H);
    
    /* Setup LVGL input driver (mouse as touchscreen) */
    lv_indev_drv_t indev_drv;
    sdl_indev_drv_init(&indev_drv);
    indev = lv_indev_drv_register(&indev_drv);
    printf("[Simulator] Input device registered (mouse as touch)\n");
    
    /* Initialize Deskimon UI */
    if (init_deskimon_ui() != ESP_OK) {
        printf("[Simulator] Failed to initialize Deskimon UI\n");
        return 1;
    }
    
    /* Start HTTP control server for external control (Swift Agent, etc.) */
    printf("[Simulator] Starting HTTP control server on port 8080...\n");
    if (!http_control_start(8080)) {
        printf("[Simulator] Warning: Failed to start HTTP server (continuing without it)\n");
    }
    
    printf("\n[Simulator] Running... (Press H for help, Ctrl+C to exit)\n");
    printf("[Simulator] Auto-cycle emotions: %s (press T to toggle)\n", auto_cycle_emotions ? "ON" : "OFF");
    printf("[Simulator] Use keyboard controls for manual emotion changes\n\n");
    fflush(stdout);
    
    /* Main loop with manual tick tracking */
    uint32_t last_tick = get_time_ms();
    int loop_count = 0;
    
    while (running) {
        /* Update LVGL tick */
        uint32_t now = get_time_ms();
        uint32_t elapsed = now - last_tick;
        if (elapsed > 0) {
            lv_tick_inc(elapsed);
            last_tick = now;
        }
        
        /* Handle LVGL tasks */
        lv_timer_handler();
        
        /* Demo: cycle through emotions */
        demo_emotions();
        
        if (loop_count % 1000 == 0) {
            printf("[Simulator] Loop iteration: %d (tick: %u ms)\n", loop_count, lv_tick_get());
            fflush(stdout);
        }
        loop_count++;
        
        /* Small delay to avoid 100% CPU usage */
        usleep(5000);  /* 5ms = ~200 FPS max */
    }
    
    printf("\n[Simulator] Shutting down...\n");
    return 0;
}
