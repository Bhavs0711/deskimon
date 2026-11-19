#include "eye_layer_renderer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define EYE_SCALE 1.35f
#define EYE_GAP_SCALE 1.15f

static inline int16_t scale_val(int16_t val) {
    return (int16_t)(((float)val) * EYE_SCALE);
}

static inline int16_t scale_gap(int16_t val) {
    return (int16_t)(((float)val) * EYE_GAP_SCALE);
}

static inline size_t alpha4_bytes(int16_t width, int16_t height) {
    return (size_t)(((width + 1) / 2) * height);
}

static inline uint8_t alpha4_get(const uint8_t *buf, int16_t width, int16_t x, int16_t y) {
    size_t idx = (size_t)y * (size_t)((width + 1) / 2) + (size_t)(x / 2);
    uint8_t b = buf[idx];
    return (x & 1) ? (b & 0x0F) : ((b >> 4) & 0x0F);
}

static inline void alpha4_set(uint8_t *buf, int16_t width, int16_t x, int16_t y, uint8_t v) {
    size_t idx = (size_t)y * (size_t)((width + 1) / 2) + (size_t)(x / 2);
    v &= 0x0F;
    if (x & 1) {
        buf[idx] = (buf[idx] & 0xF0) | v;
    } else {
        buf[idx] = (buf[idx] & 0x0F) | (v << 4);
    }
}

void eye_layer_renderer_init(EYE_LAYER_RENDERER_T *rend, lv_obj_t *canvas) {
    memset(rend, 0, sizeof(*rend));
    rend->canvas = canvas;
}

static bool ensure_buffer(EYE_LAYER_RENDERER_T *rend, int16_t width, int16_t height) {
    if (!rend->canvas) return false;
    if (rend->alpha_buf && width == rend->width && height == rend->height) return true;

    if (rend->alpha_buf) {
        free(rend->alpha_buf);
        rend->alpha_buf = NULL;
    }

    size_t bytes = alpha4_bytes(width, height);
    rend->alpha_buf = (uint8_t *)malloc(bytes);
    if (!rend->alpha_buf) return false;

    rend->width = width;
    rend->height = height;
    lv_canvas_set_buffer(rend->canvas, rend->alpha_buf, rend->width, rend->height, LV_IMG_CF_ALPHA_4BIT);
    lv_obj_set_size(rend->canvas, rend->width, rend->height);
    lv_obj_set_style_img_recolor_opa(rend->canvas, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(rend->canvas, LV_OPA_TRANSP, 0);
    return true;
}

static void clear_buffer(EYE_LAYER_RENDERER_T *rend) {
    if (!rend->alpha_buf || rend->width <= 0 || rend->height <= 0) return;
    memset(rend->alpha_buf, 0, alpha4_bytes(rend->width, rend->height));
}

static void set_mask_alpha(EYE_LAYER_RENDERER_T *rend, int16_t x, int16_t y, uint8_t src_alpha, bool subtract) {
    if (!rend->alpha_buf || x < 0 || y < 0 || x >= rend->width || y >= rend->height) return;
    uint8_t dst4 = alpha4_get(rend->alpha_buf, rend->width, x, y);
    uint8_t dst8 = (uint8_t)(dst4 * 17U);
    uint8_t out8 = 0;
    if (subtract) {
        out8 = (uint8_t)(((uint16_t)dst8 * (uint16_t)(255 - src_alpha)) / 255U);
    } else {
        out8 = (uint8_t)(src_alpha + (((uint16_t)dst8 * (uint16_t)(255 - src_alpha)) / 255U));
    }
    uint8_t out4 = (uint8_t)((out8 + 8U) / 17U);
    alpha4_set(rend->alpha_buf, rend->width, x, y, out4);
}

static void blend_rounded_rect(EYE_LAYER_RENDERER_T *rend, int16_t cx, int16_t cy, int16_t w, int16_t h, int16_t radius, int16_t angle_deg, uint8_t opacity, bool subtract) {
    if (!rend->alpha_buf || opacity == 0) return;

    float rad = angle_deg * 0.01745329252f;
    float sin_a = sinf(rad);
    float cos_a = cosf(rad);
    float half_w = w / 2.0f;
    float half_h = h / 2.0f;
    float min_half = half_w < half_h ? half_w : half_h;
    float rr = radius < min_half ? (float)radius : min_half;

    int16_t bound_x = (int16_t)(ceilf(fabsf(half_w * cos_a) + fabsf(half_h * sin_a))) + 2;
    int16_t bound_y = (int16_t)(ceilf(fabsf(half_w * sin_a) + fabsf(half_h * cos_a))) + 2;

    float inner_w = half_w - rr;
    float inner_h = half_h - rr;
    float rr_sq = rr * rr;

    if (angle_deg == 0) {
        // Fast path for 0 degrees rotation
        for (int16_t y = cy - bound_y; y <= cy + bound_y; ++y) {
            float abs_y = fabsf((float)(y - cy));
            if (abs_y > half_h) continue;

            for (int16_t x = cx - bound_x; x <= cx + bound_x; ++x) {
                float abs_x = fabsf((float)(x - cx));
                if (abs_x > half_w) continue;

                if (abs_x <= inner_w || abs_y <= inner_h) {
                    set_mask_alpha(rend, x, y, opacity, subtract);
                } else {
                    float dist_x = abs_x - inner_w;
                    float dist_y = abs_y - inner_h;
                    if ((dist_x * dist_x + dist_y * dist_y) <= rr_sq) {
                        set_mask_alpha(rend, x, y, opacity, subtract);
                    }
                }
            }
        }
    } else {
        // Rotated path with hoisted vector step math
        for (int16_t y = cy - bound_y; y <= cy + bound_y; ++y) {
            float dy = (float)(y - cy);
            float start_dx = (float)(cx - bound_x) - (float)cx;
            
            float local_x = start_dx * cos_a + dy * sin_a;
            float local_y = -start_dx * sin_a + dy * cos_a;

            for (int16_t x = cx - bound_x; x <= cx + bound_x; ++x) {
                float ax = fabsf(local_x);
                float ay = fabsf(local_y);
                
                if (ax <= half_w && ay <= half_h) {
                    if (ax <= inner_w || ay <= inner_h) {
                        set_mask_alpha(rend, x, y, opacity, subtract);
                    } else {
                        float dist_x = ax - inner_w;
                        float dist_y = ay - inner_h;
                        if ((dist_x * dist_x + dist_y * dist_y) <= rr_sq) {
                            set_mask_alpha(rend, x, y, opacity, subtract);
                        }
                    }
                }
                // Step our local affine transformed coordinates linearly!
                local_x += cos_a;
                local_y -= sin_a;
            }
        }
    }
}

static int16_t side_x(EYE_LAYER_RENDERER_T *rend, const EYE_SCENE_SPEC_T *scene, const RUNTIME_STATE_T *state, const RENDER_OP_T *op, bool right) {
    int16_t screen_w = rend->width; // Canvas is now full-screen
    int16_t center_x = screen_w / 2;
    int16_t scaled_gap = scale_gap(scene->geometry.eye_gap);
    int16_t eye_center = right ? center_x + scaled_gap : center_x - scaled_gap;
    int16_t offset_x = scale_val((right && op->mirror_x_for_right) ? -op->offset.x : op->offset.x);
    int16_t pupil_off = 0;
    if (op->is_pupil) {
        pupil_off = scale_val((right && op->mirror_x_for_right) ? -state->pupil_x : state->pupil_x);
    }
    return eye_center + offset_x + scale_val(state->drift_x + state->saccade_x) + pupil_off - rend->origin_x;
}

static int16_t side_y(EYE_LAYER_RENDERER_T *rend, const EYE_SCENE_SPEC_T *scene, const RUNTIME_STATE_T *state, const RENDER_OP_T *op) {
    int16_t pupil_off = scale_val(op->is_pupil ? state->pupil_y : 0);
    int16_t screen_h = rend->height;
    int16_t center_y = screen_h / 2;
    // Assume original base_y was the center of the viewport
    return center_y + scale_val(op->offset.y + state->drift_y + state->saccade_y) + pupil_off;
}

static int16_t side_angle(const RENDER_OP_T *op, bool right) {
    if (!right || !op->mirror_angle_for_right) return op->angle_deg;
    return -op->angle_deg;
}

static void draw_op(EYE_LAYER_RENDERER_T *rend, const EYE_SCENE_SPEC_T *scene, const RUNTIME_STATE_T *state, const RENDER_OP_T *op, bool right) {
    int16_t w = op->size.w;
    int16_t h = op->size.h;
    if (op->op == LAYER_OP_DRAW_GLOW) {
        w = (int16_t)(w + state->pulse_shift);
        h = (int16_t)(h + (state->pulse_shift / 2));
    } else if (op->op == LAYER_OP_DRAW_MID) {
        w = (int16_t)(w + (state->pulse_shift / 2));
    }

    bool is_wow = strcmp(scene->name, "eve_wow") == 0;
    if (is_wow && op->color.mode != COLOR_MODE_SCENE_BACKGROUND) {
        w = (int16_t)(w + state->pulse_shift * 2);
        h = (int16_t)(h + state->pulse_shift * 2);
    }

    w = scale_val(w);
    h = scale_val(h);

    if (w < 4) w = 4;
    if (h < 4) h = 4;
    if (state->blink && op->op == LAYER_OP_DRAW_CORE) {
        h = state->blink_height > 0 ? scale_val(state->blink_height) : 8;
    }

    bool subtract = (op->op == LAYER_OP_DRAW_CUT) || (op->color.mode == COLOR_MODE_SCENE_BACKGROUND);
    int16_t min_wh = w < h ? w : h;
    int16_t radius = op->primitive == PRIMITIVE_ELLIPSE ? (int16_t)(min_wh / 2) : scale_val(op->radius);
    if (radius < 0) radius = 0;

    int16_t cy = side_y(rend, scene, state, op);
    if (is_wow && op->color.mode != COLOR_MODE_SCENE_BACKGROUND) {
        cy -= scale_val(state->pulse_shift) / 2;
    }

    blend_rounded_rect(rend, side_x(rend, scene, state, op, right), cy, w, h, radius, side_angle(op, right), op->opacity, subtract);
}

static void draw_side(EYE_LAYER_RENDERER_T *rend, const EYE_SCENE_SPEC_T *scene, const RUNTIME_STATE_T *state, bool right) {
    for (size_t i = 0; i < scene->op_count; ++i) {
        const RENDER_OP_T *op = &scene->ops[i];
        if (!op->enabled) continue;
        if (op->side == SIDE_LEFT && right) continue;
        if (op->side == SIDE_RIGHT && !right) continue;
        draw_op(rend, scene, state, op, right);
    }
}

static void apply_row_shift(EYE_LAYER_RENDERER_T *rend, int16_t start_y, int16_t height, int16_t offset) {
    if (!rend->alpha_buf || height <= 0 || offset == 0) return;
    if (start_y < 0) start_y = 0;
    if (start_y >= rend->height) return;
    if (start_y + height > rend->height) height = rend->height - start_y;
    uint8_t temp[180] = {0};
    if (rend->width > 180) return;

    for (int16_t y = start_y; y < start_y + height; ++y) {
        for (int16_t x = 0; x < rend->width; ++x) {
            temp[x] = alpha4_get(rend->alpha_buf, rend->width, x, y);
            alpha4_set(rend->alpha_buf, rend->width, x, y, 0);
        }
        for (int16_t x = 0; x < rend->width; ++x) {
            int16_t src_x = x - offset;
            if (src_x >= 0 && src_x < rend->width) {
                alpha4_set(rend->alpha_buf, rend->width, x, y, temp[src_x]);
            }
        }
    }
}

static void apply_scanlines(EYE_LAYER_RENDERER_T *rend, const RUNTIME_STATE_T *state) {
    for (uint8_t i = 0; i < state->glitch_scanline_count && i < 4; ++i) {
        int16_t y0 = state->glitch_scanline_y[i];
        int16_t h = state->glitch_scanline_h[i] < 1 ? 1 : state->glitch_scanline_h[i];
        for (int16_t y = y0; y < y0 + h && y < rend->height; ++y) {
            if (y < 0) continue;
            for (int16_t x = 0; x < rend->width; ++x) {
                set_mask_alpha(rend, x, y, 90, false);
            }
        }
    }
}

bool eye_layer_renderer_render_eye(EYE_LAYER_RENDERER_T *rend, const EYE_SCENE_SPEC_T *scene, const RUNTIME_STATE_T *state) {
    // Single unified canvas for exactly 466x466
    int16_t width = 466;
    int16_t height = 466;
    if (!ensure_buffer(rend, width, height)) {
        return false;
    }

    rend->origin_x = 0; // Unified origin
    RUNTIME_STATE_T local_state = *state;

    for (uint8_t i = 0; i < local_state.glitch_scanline_count && i < 4; ++i) {
        local_state.glitch_scanline_y[i] = scale_val(local_state.glitch_scanline_y[i]);
        local_state.glitch_scanline_h[i] = scale_val(local_state.glitch_scanline_h[i]);
    }
    local_state.glitch_row_y = scale_val(local_state.glitch_row_y);
    local_state.glitch_row_h = scale_val(local_state.glitch_row_h);
    local_state.glitch_row_offset = scale_val(local_state.glitch_row_offset);

    if (lv_obj_get_parent(rend->canvas)) {
        lv_obj_set_style_bg_color(lv_obj_get_parent(rend->canvas), lv_color_hex(scene->background_hex), 0);
        lv_obj_set_style_bg_opa(lv_obj_get_parent(rend->canvas), LV_OPA_COVER, 0);
    }
    lv_obj_set_style_bg_color(rend->canvas, lv_color_hex(scene->background_hex), 0);
    lv_obj_set_style_bg_opa(rend->canvas, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(rend->canvas, state->eye_color, 0);
    lv_obj_set_style_opa(rend->canvas, state->output_opa ? state->output_opa : LV_OPA_COVER, 0);

    clear_buffer(rend);
    
    // Draw both eyes onto the exact same buffer!
    draw_side(rend, scene, &local_state, false);
    draw_side(rend, scene, &local_state, true);

    if (local_state.glitch_row_shift) {
        apply_row_shift(rend, local_state.glitch_row_y, local_state.glitch_row_h, local_state.glitch_row_offset);
    }
    if (local_state.glitch_scanline_count > 0) {
        apply_scanlines(rend, &local_state);
    }

    lv_obj_invalidate(rend->canvas);
    return true;
}
