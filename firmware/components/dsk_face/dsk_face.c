/*
 * Deskimon face renderer - 30 emotional states built from LVGL objects (no bitmaps).
 *
 * Architecture
 *   pose_t         static description of an emotion (eye geometry, lids, brows, mouth, colours)
 *   FX_* flags     procedural motion applied on top (blink, gaze wander, breathing, bounce, ...)
 *   frame()        one LVGL timer: eases the current pose toward the target pose (exponential
 *                  smoothing), runs the modulators and writes object properties.
 *   Shape changes  (round eyes <-> happy arcs <-> X eyes) go through a quick blink, so the
 *                  transition looks natural instead of popping.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_random.h"
#include "sdkconfig.h"
#include "dsk_face.h"
#include "dsk_board.h"

#define W        360
#define H        360
#define CX       180
#define CY       168
#define EYE_DX   76
#define EYE_DY   (-22)
#define MOUTH_DY 66
#define N_CONF   14
#define N_SPARK  4

/* ---- pose description ------------------------------------------------------------------------ */
typedef enum { SH_ROUND = 0, SH_ARC_UP, SH_ARC_DOWN, SH_XX } eye_shape_t;

enum {
    FX_BLINK = 1u << 0,  FX_BLINK_SLOW = 1u << 1, FX_WANDER = 1u << 2,  FX_WANDER_SLOW = 1u << 3,
    FX_THINK = 1u << 4,  FX_BREATH = 1u << 5,     FX_BREATH_SLOW = 1u << 6, FX_BOUNCE = 1u << 7,
    FX_BOUNCE_SMALL = 1u << 8, FX_SHAKE = 1u << 9, FX_SHAKE_SMALL = 1u << 10, FX_PULSE = 1u << 11,
    FX_FLASH = 1u << 12, FX_SPINNER = 1u << 13,   FX_TALK = 1u << 14,   FX_ZZZ = 1u << 15,
    FX_CONFETTI = 1u << 16, FX_SPARKLE = 1u << 17, FX_CHECK = 1u << 18, FX_EXCL = 1u << 19,
    FX_QUESTION = 1u << 20, FX_DOTS = 1u << 21,   FX_SCAN = 1u << 22,   FX_ORBIT = 1u << 23,
    FX_KICK = 1u << 24,
};

typedef struct {
    eye_shape_t shape;
    float ew, eh;          /* eye size (px)                                              */
    float er;              /* corner roundness 0..1 (1 = full pill/circle)               */
    float ir;              /* iris radius (px)                                           */
    float gx, gy;          /* base gaze -1..1                                            */
    float lid;             /* upper lid cover 0..1                                       */
    float slant;           /* -1..1, + = inner end lower (determined), - = inner higher (sad) */
    float lidb;            /* lower lid cover 0..1 (cheek squint)                        */
    float asym;            /* right eye height = eh*(1+asym), left = eh*(1-asym*0.5)     */
    float brow_dy;         /* px offset (negative = raised)                              */
    float brow_ang;        /* px, + = inner end lower                                    */
    float brow_op;         /* 0..1                                                       */
    float m_curve;         /* -1 frown .. +1 big smile                                   */
    float m_w;             /* mouth width px                                             */
    float m_open;          /* mouth opening px (0 = closed)                              */
    uint32_t iris_col;
    uint32_t ring_col;
    float ring_op;         /* 0..1                                                       */
    float y_off;           /* whole-face vertical offset                                 */
    uint32_t fx;
} pose_t;

#define BLUE   0x3FA9FF
#define CYAN   0x22D3EE
#define GREEN  0x34D399
#define AMBER  0xFBBF24
#define RED    0xFF4D4D
#define PURPLE 0xA78BFA
#define MINT   0x6EE7B7
#define SKY    0x7DD3FC

/*            shape      ew   eh   er   ir   gx    gy    lid   slant lidb  asym  bdy   bang  bop   mcrv  mw   mopen iris   ring    rop  yoff fx */
static const pose_t k_pose[DSK_EMO_COUNT] = {
 [DSK_EMO_NEUTRAL]     = {SH_ROUND, 92, 104, .70, 34,  0,    0,    0,    0,    0,    0,    0,    0,   0,   .35, 50,  0,  BLUE,   0,      0,   0,  FX_BLINK | FX_WANDER | FX_BREATH},
 [DSK_EMO_HAPPY]       = {SH_ROUND, 94, 100, .75, 34,  0,    0,    0,    0,    .24,  0,    0,    0,   0,   .85, 70,  0,  0x4FC3F7, 0,    0,   0,  FX_BLINK | FX_WANDER | FX_BREATH},
 [DSK_EMO_VERY_HAPPY]  = {SH_ARC_UP, 96, 96, 1,   34,  0,    0,    0,    0,    0,    0,    0,    0,   0,   1,   92,  20, AMBER,  0,      0,   -2, FX_BREATH | FX_BOUNCE_SMALL | FX_SPARKLE},
 [DSK_EMO_EXCITED]     = {SH_ROUND, 100, 116, .85, 40, 0,    -.1,  0,    0,    0,    0,    -14,  -3,  1,   1,   84,  18, AMBER,  AMBER,  .35, 0,  FX_BLINK | FX_BOUNCE | FX_WANDER | FX_KICK},
 [DSK_EMO_FOCUSED]     = {SH_ROUND, 96, 80,  .45, 30,  0,    0,    .22,  .28,  0,    0,    -2,   5,   1,   0,   36,  0,  CYAN,   0,      0,   0,  FX_BLINK_SLOW | FX_BREATH},
 [DSK_EMO_THINKING]    = {SH_ROUND, 90, 96,  .65, 30,  .55,  -.6,  .18,  -.1,  0,    0,    -8,   -2,  1,  -.05, 30,  0,  PURPLE, 0,      0,   0,  FX_BLINK | FX_THINK | FX_DOTS},
 [DSK_EMO_LISTENING]   = {SH_ROUND, 100, 116, .75, 38, 0,    0,    0,    0,    0,    0,    -10,  0,   .6,  .15, 34,  0,  CYAN,   CYAN,   .9,  0,  FX_BLINK_SLOW | FX_BREATH | FX_PULSE},
 [DSK_EMO_SPEAKING]    = {SH_ROUND, 92, 104, .70, 34,  0,    0,    0,    0,    0,    0,    0,    0,   0,   .5,  56,  14, BLUE,   0,      0,   0,  FX_BLINK | FX_WANDER | FX_TALK},
 [DSK_EMO_PROCESSING]  = {SH_ROUND, 86, 92,  .65, 30,  0,    0,    .28,  0,    0,    0,    0,    0,   0,   0,   30,  0,  AMBER,  AMBER,  .5,  0,  FX_SPINNER | FX_SCAN | FX_BREATH},
 [DSK_EMO_CONFUSED]    = {SH_ROUND, 90, 100, .65, 32,  -.4,  -.2,  0,    0,    0,    .35,  -6,   6,   1,  -.3, 46,  0,  SKY,    0,      0,   0,  FX_BLINK | FX_WANDER_SLOW | FX_QUESTION},
 [DSK_EMO_CONCERNED]   = {SH_ROUND, 92, 100, .70, 34,  0,    .25,  .12,  -.55, 0,    0,    -10,  -7,  1,  -.45, 44,  0,  SKY,    0,      0,   0,  FX_BLINK | FX_BREATH},
 [DSK_EMO_WORRIED]     = {SH_ROUND, 92, 104, .70, 38,  0,    .15,  .20,  -.8,  0,    0,    -14,  -10, 1,  -.7, 50,  0,  SKY,    0,      0,   0,  FX_BLINK | FX_SHAKE_SMALL | FX_BREATH},
 [DSK_EMO_TIRED]       = {SH_ROUND, 96, 90,  .70, 32,  0,    .3,   .45,  -.25, 0,    0,    0,    0,   0,  -.1,  36,  0,  0x93C5FD, 0,   0,   4,  FX_BLINK_SLOW | FX_BREATH_SLOW},
 [DSK_EMO_SLEEPY]      = {SH_ROUND, 96, 90,  .70, 32,  0,    .35,  .62,  0,    0,    0,    0,    0,   0,   0,   24,  6,  0x93C5FD, 0,   0,   6,  FX_BLINK_SLOW | FX_BREATH_SLOW},
 [DSK_EMO_SLEEPING]    = {SH_ARC_DOWN, 96, 60, 1, 32,  0,    0,    0,    0,    0,    0,    0,    0,   0,   .1,  24,  0,  0x93C5FD, 0,   0,   8,  FX_BREATH_SLOW | FX_ZZZ},
 [DSK_EMO_SURPRISED]   = {SH_ROUND, 104, 124, .95, 20, 0,    0,    0,    0,    0,    0,    -22,  0,   1,   0,   30,  34, SKY,    0,      0,   -2, FX_KICK | FX_BLINK_SLOW},
 [DSK_EMO_PROUD]       = {SH_ARC_UP, 96, 96, 1,   34,  0,    0,    0,    0,    0,    0,    0,    0,   0,   .7,  56,  0,  AMBER,  0,      0,   -5, FX_BREATH | FX_SPARKLE},
 [DSK_EMO_CELEBRATING] = {SH_ARC_UP, 100, 100, 1, 34,  0,    0,    0,    0,    0,    0,    0,    0,   0,   1,   100, 26, AMBER,  0,      0,   0,  FX_CONFETTI | FX_BOUNCE | FX_SPARKLE},
 [DSK_EMO_CURIOUS]     = {SH_ROUND, 94, 108, .70, 36,  .35,  -.15, 0,    0,    0,    -.2,  -10,  -2,  1,   .15, 24,  10, CYAN,   0,      0,   0,  FX_BLINK | FX_WANDER},
 [DSK_EMO_WAITING]     = {SH_ROUND, 92, 104, .70, 34,  .15,  -.05, 0,    0,    0,    0,    0,    0,   0,   .3,  44,  0,  BLUE,   0,      0,   0,  FX_BLINK | FX_WANDER | FX_BREATH},
 [DSK_EMO_ALERT]       = {SH_ROUND, 100, 120, .90, 26, 0,    0,    0,    0,    0,    0,    -16,  3,   1,   0,   30,  20, AMBER,  AMBER,  1,   0,  FX_FLASH | FX_SHAKE | FX_KICK},
 [DSK_EMO_DEADLINE_WARNING] = {SH_ROUND, 96, 100, .60, 28, 0, 0,    .15,  .5,   0,    0,    -2,   8,   1,  -.2, 56,  0,  RED,    RED,    1,   0,  FX_FLASH | FX_EXCL | FX_SHAKE_SMALL},
 [DSK_EMO_SUCCESS]     = {SH_ARC_UP, 96, 96, 1,   34,  0,    0,    0,    0,    0,    0,    0,    0,   0,   .9,  80,  0,  GREEN,  GREEN,  .9,  -2, FX_CHECK | FX_KICK | FX_SPARKLE},
 [DSK_EMO_ERROR]       = {SH_XX,    90, 90,  .30, 30,  0,    0,    0,    0,    0,    0,    0,    0,   0,  -.5,  56,  0,  RED,    RED,    .8,  0,  FX_SHAKE | FX_KICK},
 [DSK_EMO_DISAPPOINTED] = {SH_ROUND, 92, 96, .70, 33,  0,    .55,  .30,  -.5,  0,    0,    0,    -6,  1,  -.7, 44,  0,  0x93C5FD, 0,   0,   8,  FX_BLINK_SLOW | FX_BREATH_SLOW},
 [DSK_EMO_MOTIVATING]  = {SH_ROUND, 100, 106, .75, 36, 0,    0,    .10,  .35,  0,    0,    -8,   6,   1,   1,   80,  8,  AMBER,  AMBER,  .45, 0,  FX_BLINK | FX_BOUNCE_SMALL | FX_SPARKLE},
 [DSK_EMO_BUSY]        = {SH_ROUND, 90, 84,  .55, 28,  0,    0,    .28,  .10,  0,    0,    0,    0,   0,   0,   34,  0,  CYAN,   0,      0,   0,  FX_SCAN | FX_BREATH},
 [DSK_EMO_CALM]        = {SH_ROUND, 94, 92,  .80, 34,  0,    0,    .30,  0,    0,    0,    0,    0,   0,   .5,  56,  0,  MINT,   0,      0,   0,  FX_BLINK_SLOW | FX_BREATH_SLOW},
 [DSK_EMO_DISCONNECTED] = {SH_ROUND, 76, 84, .70, 26,  0,    0,    .20,  -.3,  0,    0,    0,    0,   0,  -.15, 30,  0,  0x9CA3AF, AMBER, .35, 2,  FX_BLINK_SLOW | FX_WANDER_SLOW},
 [DSK_EMO_PAIRING]     = {SH_ROUND, 92, 104, .70, 34,  0,    0,    0,    0,    0,    0,    0,    0,   0,   .3,  44,  0,  BLUE,   BLUE,   .8,  0,  FX_ORBIT | FX_PULSE | FX_BLINK},
};

/* ---- runtime state --------------------------------------------------------------------------- */
typedef struct {
    lv_obj_t *box, *iris, *pupil, *hl, *lidt, *lidb, *arc, *x1, *x2;
    lv_point_t pt_t[2], pt_b[2], pt_x1[2], pt_x2[2];
} eye_t;

typedef struct {
    lv_obj_t *root;
    eye_t eye[2];
    lv_obj_t *brow[2];
    lv_point_t brow_pt[2][2];
    lv_obj_t *m_arc, *m_open, *m_flat;
    lv_obj_t *ring, *spinner;
    lv_obj_t *dot[3];
    lv_obj_t *zz[3];
    lv_obj_t *lbl_excl, *lbl_q;
    lv_obj_t *check;
    lv_point_t check_pt[3];
    lv_obj_t *conf[N_CONF];
    lv_obj_t *spark[N_SPARK];
    lv_timer_t *timer;

    pose_t cur, tgt;
    eye_shape_t shape_cur;
    dsk_emotion_t emo;
    uint32_t fx;
    uint32_t last_ms, entered_ms;
    float kick;
    /* modulators */
    float gaze_x, gaze_y, wander_x, wander_y, tilt_x, tilt_y, tilt_sx, tilt_sy;
    uint32_t next_wander_ms, next_blink_ms, blink_start_ms, blink_dur_ms;
    bool blink_active, blink_double;
    float conf_y[N_CONF], conf_x[N_CONF], conf_v[N_CONF];
    uint32_t ring_div;
    bool ready;
} face_t;

static face_t F;

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float rndf(float a, float b) { return a + (esp_random() / 4294967295.0f) * (b - a); }
static inline uint32_t rndu(uint32_t a, uint32_t b) { return a + (esp_random() % (b - a + 1)); }
static inline float ease(float cur, float tgt, float dt, float tau) { return cur + (tgt - cur) * (1.0f - expf(-dt / tau)); }

static lv_obj_t *mk(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *mk_circle(lv_obj_t *parent, uint32_t col)
{
    lv_obj_t *o = mk(parent);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    return o;
}

static lv_obj_t *mk_line(lv_obj_t *parent, uint32_t col, int width, bool rounded)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_line_color(l, lv_color_hex(col), 0);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_rounded(l, rounded, 0);
    lv_obj_set_style_line_opa(l, LV_OPA_COVER, 0);
    return l;
}

static lv_obj_t *mk_arc(lv_obj_t *parent, uint32_t col, int width)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_remove_style_all(a);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_arc_set_rotation(a, 0);
    lv_obj_set_style_arc_color(a, lv_color_hex(col), LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 0, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
    return a;
}

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *mk_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t col)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_label_set_text(l, txt);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return l;
}

/* ---- construction ---------------------------------------------------------------------------- */
static void build_eye(eye_t *e, lv_obj_t *root)
{
    e->box = mk(root);
    lv_obj_set_style_bg_color(e->box, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_clip_corner(e->box, true, 0);
    e->iris = mk_circle(e->box, BLUE);
    e->pupil = mk_circle(e->box, 0x0B1220);
    e->hl = mk_circle(e->box, 0xFFFFFF);
    e->lidt = mk_line(e->box, 0x000000, 8, false);
    e->lidb = mk_line(e->box, 0x000000, 8, false);
    e->arc = mk_arc(root, 0xF8FAFC, 15);
    e->x1 = mk_line(root, RED, 11, true);
    e->x2 = mk_line(root, RED, 11, true);
    show(e->arc, false); show(e->x1, false); show(e->x2, false);
}

esp_err_t dsk_face_init(void)
{
    memset(&F, 0, sizeof(F));
    if (!dsk_lvgl_lock(2000)) return ESP_ERR_TIMEOUT;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    F.root = mk(scr);
    lv_obj_set_size(F.root, W, H);
    lv_obj_set_pos(F.root, 0, 0);
    lv_obj_set_style_bg_opa(F.root, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(F.root, LV_OBJ_FLAG_CLICKABLE);        /* receives taps for the app */

#if CONFIG_DSK_FACE_RING
    F.ring = mk_arc(F.root, CYAN, 7);
    lv_obj_set_size(F.ring, 352, 352); lv_obj_set_pos(F.ring, 4, 4);
    lv_arc_set_bg_angles(F.ring, 0, 360);
    F.spinner = mk_arc(F.root, AMBER, 9);
    lv_obj_set_size(F.spinner, 332, 332); lv_obj_set_pos(F.spinner, 14, 14);
    lv_arc_set_bg_angles(F.spinner, 0, 80);
    show(F.ring, false); show(F.spinner, false);
#endif

    for (int i = 0; i < 2; i++) {
        build_eye(&F.eye[i], F.root);
        F.brow[i] = mk_line(F.root, 0xE5E7EB, 9, true);
    }
    F.m_arc = mk_arc(F.root, 0xF1F5F9, 9);
    F.m_open = mk(F.root);
    lv_obj_set_style_bg_color(F.m_open, lv_color_hex(0xFF6B81), 0);
    F.m_flat = mk(F.root);
    lv_obj_set_style_bg_color(F.m_flat, lv_color_hex(0xF1F5F9), 0);
    lv_obj_set_style_radius(F.m_flat, 4, 0);

    for (int i = 0; i < 3; i++) {
        F.dot[i] = mk_circle(F.root, PURPLE);
        lv_obj_set_size(F.dot[i], 12, 12);
        lv_obj_set_pos(F.dot[i], CX - 30 + i * 24 - 6, 34);
        show(F.dot[i], false);
        F.zz[i] = mk_label(F.root, i == 0 ? "z" : "Z", i == 2 ? &lv_font_montserrat_28 : &lv_font_montserrat_20, 0x93C5FD);
        show(F.zz[i], false);
    }
    F.lbl_excl = mk_label(F.root, "!", &lv_font_montserrat_48, RED);
    lv_obj_set_pos(F.lbl_excl, CX - 8, 14); show(F.lbl_excl, false);
    F.lbl_q = mk_label(F.root, "?", &lv_font_montserrat_48, SKY);
    lv_obj_set_pos(F.lbl_q, 262, 38); show(F.lbl_q, false);

    F.check = mk_line(F.root, GREEN, 12, true);
    F.check_pt[0] = (lv_point_t){4, 20}; F.check_pt[1] = (lv_point_t){20, 36}; F.check_pt[2] = (lv_point_t){50, 4};
    lv_line_set_points(F.check, F.check_pt, 3);
    lv_obj_set_size(F.check, 60, 44);
    lv_obj_set_pos(F.check, CX - 30, 20); show(F.check, false);

    static const uint32_t conf_cols[] = {0xFBBF24, 0xF472B6, 0x34D399, 0x60A5FA, 0xA78BFA, 0xFB7185};
    for (int i = 0; i < N_CONF; i++) {
        F.conf[i] = mk(F.root);
        lv_obj_set_size(F.conf[i], 7, 12);
        lv_obj_set_style_radius(F.conf[i], 2, 0);
        lv_obj_set_style_bg_color(F.conf[i], lv_color_hex(conf_cols[i % 6]), 0);
        F.conf_x[i] = rndf(20, 340); F.conf_y[i] = rndf(-60, 330); F.conf_v[i] = rndf(60, 130);
        show(F.conf[i], false);
    }
    static const lv_point_t spark_pos[N_SPARK] = {{60, 80}, {300, 70}, {44, 200}, {316, 210}};
    for (int i = 0; i < N_SPARK; i++) {
        F.spark[i] = mk_circle(F.root, 0xFEF3C7);
        lv_obj_set_size(F.spark[i], 8, 8);
        lv_obj_set_pos(F.spark[i], spark_pos[i].x, spark_pos[i].y);
        show(F.spark[i], false);
    }

    F.emo = DSK_EMO_DISCONNECTED;
    F.cur = F.tgt = k_pose[F.emo];
    F.fx = F.tgt.fx;
    F.shape_cur = F.cur.shape;
    F.last_ms = F.entered_ms = lv_tick_get();
    F.next_blink_ms = F.last_ms + 1500;
    F.timer = lv_timer_create(NULL, 1000 / CONFIG_DSK_FACE_FPS, NULL);  /* replaced below */
    lv_timer_del(F.timer);
    extern void dsk_face_frame_cb(lv_timer_t *t);
    F.timer = lv_timer_create(dsk_face_frame_cb, 1000 / CONFIG_DSK_FACE_FPS, NULL);
    F.ready = true;
    dsk_lvgl_unlock();
    return ESP_OK;
}

lv_obj_t *dsk_face_root(void) { return F.root; }
dsk_emotion_t dsk_face_get_emotion(void) { return F.emo; }

void dsk_face_set_tilt(float tx, float ty)
{
    F.tilt_x = clampf(tx, -1, 1);
    F.tilt_y = clampf(ty, -1, 1);
}

void dsk_face_set_emotion(dsk_emotion_t e)
{
    if (!F.ready || e >= DSK_EMO_COUNT) return;
    if (!dsk_lvgl_lock(500)) return;
    if (e != F.emo) {
        F.emo = e;
        F.tgt = k_pose[e];
        F.fx = F.tgt.fx;
        F.entered_ms = lv_tick_get();
        F.kick = (F.fx & FX_KICK) ? 1.0f : 0.0f;
        if (F.fx & FX_SPARKLE) for (int i = 0; i < N_SPARK; i++) lv_obj_clear_flag(F.spark[i], LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < N_SPARK; i++) if (!(F.fx & FX_SPARKLE)) lv_obj_add_flag(F.spark[i], LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 3; i++) { show(F.dot[i], F.fx & FX_DOTS); show(F.zz[i], F.fx & FX_ZZZ); }
        for (int i = 0; i < N_CONF; i++) show(F.conf[i], F.fx & FX_CONFETTI);
        show(F.lbl_excl, F.fx & FX_EXCL);
        show(F.lbl_q, F.fx & FX_QUESTION);
        show(F.check, F.fx & FX_CHECK);
        F.next_wander_ms = 0;
    }
    dsk_lvgl_unlock();
}

/* ---- per-frame update ------------------------------------------------------------------------ */
static void set_line(lv_obj_t *l, lv_point_t *pts, int n, int x0, int y0, int x1, int y1)
{
    /* position the line object at its bounding box so invalidation stays small */
    lv_obj_set_pos(l, x0, y0);
    lv_obj_set_size(l, x1 - x0 + 1, y1 - y0 + 1);
    lv_line_set_points(l, pts, n);
}

static void update_eye(int idx, float t, float blink, float ey_scale)
{
    eye_t *e = &F.eye[idx];
    const pose_t *c = &F.cur;
    bool left = (idx == 0);
    float hmul = left ? (1.0f - c->asym * 0.5f) : (1.0f + c->asym);
    float ew = c->ew * ey_scale, eh = c->eh * hmul * ey_scale;
    float ex = CX + (left ? -EYE_DX : EYE_DX) + F.wander_x * 0;       /* eye centre */
    float ey = CY + EYE_DY + c->y_off + F.cur.y_off * 0;
    ex += (float)(0);
    ey += 0;
    /* face-level offsets are added by the caller via ey_off/ex_off in F.* fields */
    ex += F.gaze_x * 0;   /* keep eyes anchored; gaze moves the iris only */

    float lid = clampf(fmaxf(c->lid, blink), 0, 1);
    float rad = c->er * fminf(ew, eh) * 0.5f;

    bool arcs = (F.shape_cur == SH_ARC_UP || F.shape_cur == SH_ARC_DOWN);
    bool xx = (F.shape_cur == SH_XX);
    show(e->box, !arcs && !xx);
    show(e->arc, arcs);
    show(e->x1, xx); show(e->x2, xx);

    float ox = F.cur.gx * 0, oy = 0; (void)ox; (void)oy;

    if (!arcs && !xx) {
        int bx = (int)(ex - ew / 2), by = (int)(ey - eh / 2);
        lv_obj_set_pos(e->box, bx + (int)F.conf_x[0] * 0, by);
        lv_obj_set_size(e->box, (int)ew, (int)eh);
        lv_obj_set_style_radius(e->box, (lv_coord_t)rad, 0);

        float ir = c->ir * ey_scale;
        float travel_x = fmaxf(0, ew / 2 - ir * 0.55f) * 0.85f;
        float travel_y = fmaxf(0, eh / 2 - ir * 0.55f) * 0.85f;
        float ix = ew / 2 + F.gaze_x * travel_x;
        float iy = eh / 2 + F.gaze_y * travel_y;
        lv_obj_set_size(e->iris, (int)(ir * 2), (int)(ir * 2));
        lv_obj_set_pos(e->iris, (int)(ix - ir), (int)(iy - ir));
        lv_obj_set_style_bg_color(e->iris, lv_color_hex(c->iris_col), 0);
        float pr = ir * 0.48f;
        lv_obj_set_size(e->pupil, (int)(pr * 2), (int)(pr * 2));
        lv_obj_set_pos(e->pupil, (int)(ix - pr), (int)(iy - pr));
        float hr = fmaxf(3, ir * 0.2f);
        lv_obj_set_size(e->hl, (int)(hr * 2), (int)(hr * 2));
        lv_obj_set_pos(e->hl, (int)(ix - ir * 0.38f - hr), (int)(iy - ir * 0.38f - hr));

        /* upper lid: black thick line = region above the lid edge is covered */
        float cover = lid * eh;
        float s = c->slant * eh * 0.30f;
        float edge_outer = cover - s, edge_inner = cover + s;
        float T = eh * 1.6f;
        if (fmaxf(edge_outer, edge_inner) > 0.5f) {
            float yl = (left ? edge_outer : edge_inner) - T / 2;
            float yr = (left ? edge_inner : edge_outer) - T / 2;
            e->pt_t[0] = (lv_point_t){-12, (lv_coord_t)(yl + T / 2 + 4)};
            e->pt_t[1] = (lv_point_t){(lv_coord_t)ew + 12, (lv_coord_t)(yr + T / 2 + 4)};
            /* object box = eye box; ext-draw size = line width keeps the coverage */
            lv_obj_set_style_line_width(e->lidt, (int)T, 0);
            e->pt_t[0].y = (lv_coord_t)yl; e->pt_t[1].y = (lv_coord_t)yr;
            lv_obj_set_pos(e->lidt, 0, 0);
            lv_obj_set_size(e->lidt, (int)ew, (int)eh);
            lv_line_set_points(e->lidt, e->pt_t, 2);
            show(e->lidt, true);
        } else show(e->lidt, false);

        /* lower lid (cheek squint) */
        float cb = c->lidb * eh;
        if (cb > 0.5f) {
            float y = eh - cb + T / 2;
            e->pt_b[0] = (lv_point_t){-12, (lv_coord_t)y};
            e->pt_b[1] = (lv_point_t){(lv_coord_t)ew + 12, (lv_coord_t)y};
            lv_obj_set_style_line_width(e->lidb, (int)T, 0);
            lv_obj_set_pos(e->lidb, 0, 0);
            lv_obj_set_size(e->lidb, (int)ew, (int)eh);
            lv_line_set_points(e->lidb, e->pt_b, 2);
            show(e->lidb, true);
        } else show(e->lidb, false);
    } else if (arcs) {
        int R = (int)(c->ew * 0.40f * ey_scale);
        lv_obj_set_size(e->arc, R * 2, R * 2);
        bool up = (F.shape_cur == SH_ARC_UP);
        lv_obj_set_pos(e->arc, (int)(ex - R), (int)(ey - R + (up ? R * 0.45f : -R * 0.45f)));
        lv_arc_set_bg_angles(e->arc, up ? 208 : 28, up ? 332 : 152);
        lv_obj_set_style_arc_color(e->arc, lv_color_hex(up ? 0xF8FAFC : 0xCBD5E1), LV_PART_MAIN);
    } else {
        int r = (int)(c->ew * 0.27f);
        e->pt_x1[0] = (lv_point_t){0, 0};  e->pt_x1[1] = (lv_point_t){r * 2, r * 2};
        e->pt_x2[0] = (lv_point_t){r * 2, 0}; e->pt_x2[1] = (lv_point_t){0, r * 2};
        lv_obj_set_pos(e->x1, (int)(ex - r), (int)(ey - r)); lv_obj_set_size(e->x1, r * 2 + 1, r * 2 + 1);
        lv_obj_set_pos(e->x2, (int)(ex - r), (int)(ey - r)); lv_obj_set_size(e->x2, r * 2 + 1, r * 2 + 1);
        lv_line_set_points(e->x1, e->pt_x1, 2); lv_line_set_points(e->x2, e->pt_x2, 2);
        lv_obj_set_style_line_color(e->x1, lv_color_hex(c->iris_col), 0);
        lv_obj_set_style_line_color(e->x2, lv_color_hex(c->iris_col), 0);
    }

    /* eyebrow */
    float bop = c->brow_op;
    if (bop > 0.03f) {
        float inner_sign = left ? 1.0f : -1.0f;       /* +1: inner end is on the right */
        float by = ey - eh / 2 - 20 + c->brow_dy;
        float x_in = ex + inner_sign * ew * 0.40f, x_out = ex - inner_sign * ew * 0.42f;
        float y_in = by + c->brow_ang, y_out = by - c->brow_ang;
        int minx = (int)fminf(x_in, x_out) - 8, maxx = (int)fmaxf(x_in, x_out) + 8;
        int miny = (int)fminf(y_in, y_out) - 8, maxy = (int)fmaxf(y_in, y_out) + 8;
        F.brow_pt[idx][0] = (lv_point_t){(lv_coord_t)(x_out - minx), (lv_coord_t)(y_out - miny)};
        F.brow_pt[idx][1] = (lv_point_t){(lv_coord_t)(x_in - minx), (lv_coord_t)(y_in - miny)};
        set_line(F.brow[idx], F.brow_pt[idx], 2, minx, miny, maxx, maxy);
        lv_obj_set_style_line_opa(F.brow[idx], (lv_opa_t)(bop * 255), 0);
        show(F.brow[idx], true);
    } else show(F.brow[idx], false);
}

static void update_mouth(float t, float talk)
{
    const pose_t *c = &F.cur;
    float my = CY + MOUTH_DY + c->y_off;
    float w = c->m_w;
    float open = c->m_open + talk;
    float curve = c->m_curve;

    /* wavy/unsure mouth for confused: small oscillation of the curve */
    if (F.emo == DSK_EMO_CONFUSED) curve = -0.2f + 0.25f * sinf(t * 5.0f);
    if (F.emo == DSK_EMO_WORRIED) curve += 0.06f * sinf(t * 18.0f);

    bool has_open = open > 2.5f;
    show(F.m_open, has_open);
    if (has_open) {
        int ow = (int)(w * (F.shape_cur == SH_ARC_UP ? 0.55f : 0.6f)), oh = (int)open;
        if (ow < 14) ow = 14;
        lv_obj_set_size(F.m_open, ow, oh);
        lv_obj_set_style_radius(F.m_open, (lv_coord_t)(fminf(ow, oh) / 2), 0);
        lv_obj_set_pos(F.m_open, (int)(CX - ow / 2), (int)(my - oh * 0.1f + fabsf(curve) * 8));
    }

    float s = fabsf(curve) * w * 0.30f;
    bool arc = (s >= 2.5f) && (!has_open || curve > 0.5f);
    show(F.m_arc, arc);
    if (arc) {
        float R = (w * w / 4 + s * s) / (2 * s);
        float half = asinf(clampf(w / (2 * R), 0, 1)) * 57.2958f;
        int D = (int)(R * 2);
        lv_obj_set_size(F.m_arc, D, D);
        if (curve > 0) {          /* smile: bottom of circle */
            lv_arc_set_bg_angles(F.m_arc, (uint16_t)(90 - half), (uint16_t)(90 + half));
            lv_obj_set_pos(F.m_arc, (int)(CX - R), (int)(my - s / 2 - R + s));
        } else {                  /* frown: top of circle */
            lv_arc_set_bg_angles(F.m_arc, (uint16_t)(270 - half), (uint16_t)(270 + half));
            lv_obj_set_pos(F.m_arc, (int)(CX - R), (int)(my + s / 2 + R - s));
        }
    }
    bool flat = !arc && !has_open;
    show(F.m_flat, flat);
    if (flat) {
        lv_obj_set_size(F.m_flat, (int)w, 7);
        lv_obj_set_pos(F.m_flat, (int)(CX - w / 2), (int)my);
    }
}

void dsk_face_frame_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = lv_tick_get();
    float dt = (now - F.last_ms) / 1000.0f;
    if (dt <= 0) return;
    if (dt > 0.2f) dt = 0.2f;
    F.last_ms = now;
    float t = now / 1000.0f;
    pose_t *c = &F.cur;
    const pose_t *g = &F.tgt;

    /* -- shape change goes through a blink -- */
    float lid_target = g->lid;
    if (F.shape_cur != g->shape) {
        lid_target = 1.0f;
        if (c->lid > 0.9f || F.shape_cur != SH_ROUND) {
            if (c->lid > 0.9f || F.shape_cur != SH_ROUND) F.shape_cur = g->shape;
        }
    }
    if (F.shape_cur != SH_ROUND && g->shape == SH_ROUND) { F.shape_cur = SH_ROUND; c->lid = 1.0f; }

    /* -- ease pose -- */
    c->ew = ease(c->ew, g->ew, dt, 0.10f);   c->eh = ease(c->eh, g->eh, dt, 0.10f);
    c->er = ease(c->er, g->er, dt, 0.12f);   c->ir = ease(c->ir, g->ir, dt, 0.10f);
    c->gx = ease(c->gx, g->gx, dt, 0.10f);   c->gy = ease(c->gy, g->gy, dt, 0.10f);
    c->lid = ease(c->lid, lid_target, dt, F.shape_cur != g->shape ? 0.04f : 0.09f);
    c->slant = ease(c->slant, g->slant, dt, 0.10f); c->lidb = ease(c->lidb, g->lidb, dt, 0.10f);
    c->asym = ease(c->asym, g->asym, dt, 0.12f);
    c->brow_dy = ease(c->brow_dy, g->brow_dy, dt, 0.10f); c->brow_ang = ease(c->brow_ang, g->brow_ang, dt, 0.10f);
    c->brow_op = ease(c->brow_op, g->brow_op, dt, 0.12f);
    c->m_curve = ease(c->m_curve, g->m_curve, dt, 0.10f); c->m_w = ease(c->m_w, g->m_w, dt, 0.10f);
    c->m_open = ease(c->m_open, g->m_open, dt, 0.08f);
    c->ring_op = ease(c->ring_op, g->ring_op, dt, 0.15f);
    c->y_off = ease(c->y_off, g->y_off, dt, 0.15f);
    c->iris_col = lv_color_to32(lv_color_mix(lv_color_hex(g->iris_col), lv_color_hex(c->iris_col), (uint8_t)(255 * (1 - expf(-dt / 0.12f))))) & 0xFFFFFF;
    c->ring_col = g->ring_col ? g->ring_col : c->ring_col;

    /* -- blink timeline -- */
    float blink = 0;
    if ((F.fx & (FX_BLINK | FX_BLINK_SLOW)) && F.shape_cur == SH_ROUND) {
        bool slow = F.fx & FX_BLINK_SLOW;
        if (!F.blink_active && now >= F.next_blink_ms) {
            F.blink_active = true; F.blink_start_ms = now;
            F.blink_dur_ms = slow ? 520 : 200;
            F.blink_double = (esp_random() % 100) < 14;
        }
        if (F.blink_active) {
            float p = (now - F.blink_start_ms) / (float)F.blink_dur_ms;
            if (p >= 1.0f) {
                F.blink_active = false;
                F.next_blink_ms = now + (F.blink_double ? 160 : rndu(slow ? 3500 : 2200, slow ? 7000 : 5800));
                F.blink_double = false;
                p = 1.0f;
            }
            blink = (p < 0.35f) ? p / 0.35f : 1.0f - (p - 0.35f) / 0.65f;
            blink = clampf(blink, 0, 1);
        }
    }

    /* -- gaze -- */
    float gx = 0, gy = 0;
    if (F.fx & FX_THINK) {
        bool a = ((now / 1500) & 1) == 0;
        F.wander_x = a ? 0.6f : -0.45f; F.wander_y = -0.65f;
    } else if (F.fx & FX_SCAN) {
        bool a = ((now / ((F.emo == DSK_EMO_BUSY) ? 260 : 700)) & 1) == 0;
        F.wander_x = a ? -0.85f : 0.85f; F.wander_y = 0.05f;
    } else if (F.fx & FX_ORBIT) {
        F.wander_x = cosf(t * 3.9f) * 0.75f; F.wander_y = sinf(t * 3.9f) * 0.75f;
    } else if (F.fx & (FX_WANDER | FX_WANDER_SLOW)) {
        if (now >= F.next_wander_ms) {
            float amp = (F.fx & FX_WANDER_SLOW) ? 0.4f : 0.7f;
            F.wander_x = rndf(-amp, amp); F.wander_y = rndf(-amp * 0.6f, amp * 0.6f);
            F.next_wander_ms = now + rndu(1400, (F.fx & FX_WANDER_SLOW) ? 4800 : 3200);
        }
    } else { F.wander_x = 0; F.wander_y = 0; }
    F.tilt_sx = ease(F.tilt_sx, F.tilt_x, dt, 0.15f);
    F.tilt_sy = ease(F.tilt_sy, F.tilt_y, dt, 0.15f);
    gx = clampf(c->gx + F.wander_x + F.tilt_sx * 0.6f, -1, 1);
    gy = clampf(c->gy + F.wander_y + F.tilt_sy * 0.6f, -1, 1);
    float gtau = (F.fx & FX_SCAN) ? 0.05f : 0.09f;
    F.gaze_x = ease(F.gaze_x, gx, dt, gtau);
    F.gaze_y = ease(F.gaze_y, gy, dt, gtau);

    /* -- kick (entry impulse) -- */
    F.kick = ease(F.kick, 0, dt, 0.22f);

    /* -- body motion: breathing, bounce, shake -- */
    float scale = 1.0f, dy = 0, dx = 0;
    if (F.fx & FX_BREATH) scale += 0.022f * sinf(t * 2.0f * 3.14159f / 3.6f);
    if (F.fx & FX_BREATH_SLOW) scale += 0.03f * sinf(t * 2.0f * 3.14159f / 5.2f);
    if (F.fx & FX_BOUNCE) dy -= 10.0f * fabsf(sinf(t * 2.0f * 3.14159f * 1.15f));
    if (F.fx & FX_BOUNCE_SMALL) dy -= 4.0f * fabsf(sinf(t * 2.0f * 3.14159f * 0.9f));
    if (F.fx & FX_SHAKE) dx += (3.0f + 6.0f * F.kick) * sinf(t * 2.0f * 3.14159f * 11.0f);
    if (F.fx & FX_SHAKE_SMALL) dx += 2.0f * sinf(t * 2.0f * 3.14159f * 9.0f);
    if (F.fx & FX_KICK) { dy -= 12.0f * F.kick; scale += 0.10f * F.kick; }
    (void)dx;

    float talk = 0;
    if (F.fx & FX_TALK) {
        float a = fabsf(sinf(t * 17.0f)) * 0.6f + fabsf(sinf(t * 9.3f + 1.3f)) * 0.4f;
        talk = 18.0f * a;
    }

    /* shift whole face by temporarily folding dx/dy into y_off (eyes read c->y_off) */
    float saved_y = c->y_off;
    c->y_off += dy;
    update_eye(0, t, blink, scale);
    update_eye(1, t, blink, scale);
    update_mouth(t, talk);
    c->y_off = saved_y;

    /* -- effects -- */
#if CONFIG_DSK_FACE_RING
    F.ring_div++;
    bool ring_on = c->ring_op > 0.04f;
    if (ring_on != !lv_obj_has_flag(F.ring, LV_OBJ_FLAG_HIDDEN)) show(F.ring, ring_on);
    bool spin = (F.fx & FX_SPINNER) != 0;
    if (spin != !lv_obj_has_flag(F.spinner, LV_OBJ_FLAG_HIDDEN)) show(F.spinner, spin);
    if ((F.ring_div % 3) == 0) {                         /* ring at ~1/3 frame rate (full-screen redraw) */
        if (ring_on) {
            float op = c->ring_op;
            if (F.fx & FX_PULSE) op *= 0.35f + 0.65f * (0.5f + 0.5f * sinf(t * 2.0f * 3.14159f * 1.2f));
            if (F.fx & FX_FLASH) op *= (sinf(t * 2.0f * 3.14159f * 2.5f) > 0) ? 1.0f : 0.25f;
            lv_obj_set_style_arc_color(F.ring, lv_color_hex(c->ring_col), LV_PART_MAIN);
            lv_obj_set_style_arc_opa(F.ring, (lv_opa_t)(clampf(op, 0, 1) * 255), LV_PART_MAIN);
        }
        if (spin) {
            int a = (int)(fmodf(t * 300.0f, 360.0f));
            lv_arc_set_bg_angles(F.spinner, a, (a + 80) % 360);
        }
    }
#endif
    if (F.fx & FX_DOTS) {
        for (int i = 0; i < 3; i++) {
            float ph = sinf(t * 6.0f - i * 1.0f);
            lv_obj_set_style_bg_opa(F.dot[i], (lv_opa_t)(90 + 165 * (0.5f + 0.5f * ph)), 0);
            lv_obj_set_y(F.dot[i], 34 - (int)(5 * (0.5f + 0.5f * ph)));
        }
    }
    if (F.fx & FX_ZZZ) {
        for (int i = 0; i < 3; i++) {
            float p = fmodf(t * 0.35f + i * 0.33f, 1.0f);
            lv_obj_set_pos(F.zz[i], 232 + (int)(p * 46) + i * 8, 120 - (int)(p * 70));
            lv_obj_set_style_text_opa(F.zz[i], (lv_opa_t)(255 * sinf(p * 3.14159f)), 0);
        }
    }
    if (F.fx & FX_EXCL) lv_obj_set_style_text_opa(F.lbl_excl, (sinf(t * 15.0f) > -0.2f) ? 255 : 80, 0);
    if (F.fx & FX_QUESTION) lv_obj_set_y(F.lbl_q, 38 + (int)(4 * sinf(t * 4.0f)));
    if (F.fx & FX_CHECK) lv_obj_set_style_line_opa(F.check, (lv_opa_t)(255 * clampf(1.0f - F.kick * 0.8f, 0, 1)), 0);
    if (F.fx & FX_SPARKLE) {
        for (int i = 0; i < N_SPARK; i++) {
            float ph = sinf(t * 5.0f + i * 1.7f);
            lv_obj_set_style_bg_opa(F.spark[i], (lv_opa_t)(255 * clampf(0.5f + 0.5f * ph, 0, 1)), 0);
            int sz = 5 + (int)(4 * (0.5f + 0.5f * ph));
            lv_obj_set_size(F.spark[i], sz, sz);
        }
    }
    if (F.fx & FX_CONFETTI) {
        for (int i = 0; i < N_CONF; i++) {
            F.conf_y[i] += F.conf_v[i] * dt;
            if (F.conf_y[i] > H + 10) { F.conf_y[i] = -12; F.conf_x[i] = rndf(20, 340); F.conf_v[i] = rndf(70, 150); }
            lv_obj_set_pos(F.conf[i], (int)(F.conf_x[i] + 8 * sinf(t * 3 + i)), (int)F.conf_y[i]);
        }
    }
}
