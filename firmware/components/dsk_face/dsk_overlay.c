#include <stdio.h>
#include <string.h>
#include "dsk_overlay.h"
#include "dsk_board.h"

static struct {
    lv_obj_t *card, *title, *sub, *icon_bar, *clock;
    lv_obj_t *ic_wifi, *ic_mac, *ic_ble;
    lv_timer_t *hide_timer;
    dsk_overlay_kind_t kind;
    bool visible;
} O;

static const struct { uint32_t col; const char *sym; } k_kind[DSK_OVL_KIND_COUNT] = {
    [DSK_OVL_INFO] = {0x3FA9FF, LV_SYMBOL_BELL},      [DSK_OVL_MEETING] = {0x34D399, LV_SYMBOL_OK},
    [DSK_OVL_REMINDER] = {0xFBBF24, LV_SYMBOL_BELL},  [DSK_OVL_ALARM] = {0xFF4D4D, LV_SYMBOL_WARNING},
    [DSK_OVL_EMAIL] = {0xA78BFA, LV_SYMBOL_ENVELOPE}, [DSK_OVL_STATUS] = {0x22D3EE, LV_SYMBOL_WIFI},
    [DSK_OVL_TIME] = {0x7DD3FC, LV_SYMBOL_BELL},      [DSK_OVL_PROCESSING] = {0xFBBF24, LV_SYMBOL_REFRESH},
    [DSK_OVL_PAIRING] = {0x3FA9FF, LV_SYMBOL_BLUETOOTH}, [DSK_OVL_MENU] = {0x93C5FD, LV_SYMBOL_LIST},
};

static void hide_cb(lv_timer_t *t) { (void)t; O.visible = false; lv_obj_add_flag(O.card, LV_OBJ_FLAG_HIDDEN); lv_timer_pause(O.hide_timer); }

esp_err_t dsk_overlay_init(void)
{
    if (!dsk_lvgl_lock(2000)) return ESP_ERR_TIMEOUT;
    lv_obj_t *top = lv_layer_top();

    O.card = lv_obj_create(top);
    lv_obj_remove_style_all(O.card);
    lv_obj_set_size(O.card, 288, 150);
    lv_obj_align(O.card, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_radius(O.card, 28, 0);
    lv_obj_set_style_bg_color(O.card, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(O.card, LV_OPA_90, 0);
    lv_obj_set_style_border_width(O.card, 3, 0);
    lv_obj_set_style_border_color(O.card, lv_color_hex(0x3FA9FF), 0);
    lv_obj_set_style_pad_all(O.card, 12, 0);
    lv_obj_clear_flag(O.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(O.card, LV_OBJ_FLAG_HIDDEN);

    O.title = lv_label_create(O.card);
    lv_obj_set_width(O.title, 260);
    lv_label_set_long_mode(O.title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(O.title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(O.title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(O.title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(O.title, LV_ALIGN_TOP_MID, 0, 8);

    O.sub = lv_label_create(O.card);
    lv_obj_set_width(O.sub, 260);
    lv_label_set_long_mode(O.sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(O.sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(O.sub, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(O.sub, lv_color_hex(0xCBD5E1), 0);
    lv_obj_align(O.sub, LV_ALIGN_BOTTOM_MID, 0, -6);

    /* status icons, top edge of the round display */
    O.icon_bar = lv_obj_create(top);
    lv_obj_remove_style_all(O.icon_bar);
    lv_obj_set_size(O.icon_bar, 120, 26);
    lv_obj_align(O.icon_bar, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_clear_flag(O.icon_bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(O.icon_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(O.icon_bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(O.icon_bar, 14, 0);
    const char *syms[3] = {LV_SYMBOL_WIFI, LV_SYMBOL_HOME, LV_SYMBOL_BLUETOOTH};
    lv_obj_t **ics[3] = {&O.ic_wifi, &O.ic_mac, &O.ic_ble};
    for (int i = 0; i < 3; i++) {
        *ics[i] = lv_label_create(O.icon_bar);
        lv_label_set_text(*ics[i], syms[i]);
        lv_obj_set_style_text_font(*ics[i], &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(*ics[i], lv_color_hex(0x475569), 0);
    }
    O.clock = lv_label_create(top);
    lv_obj_set_style_text_font(O.clock, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(O.clock, lv_color_hex(0x64748B), 0);
    lv_obj_align(O.clock, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_label_set_text(O.clock, "");

    O.hide_timer = lv_timer_create(hide_cb, 1000, NULL);
    lv_timer_pause(O.hide_timer);
    dsk_lvgl_unlock();
    return ESP_OK;
}

void dsk_overlay_show(dsk_overlay_kind_t kind, const char *title, const char *subtitle, uint32_t duration_ms)
{
    if (!O.card || kind >= DSK_OVL_KIND_COUNT || !dsk_lvgl_lock(300)) return;
    char buf[96];
    snprintf(buf, sizeof buf, "%s %s", k_kind[kind].sym, title ? title : "");
    lv_label_set_text(O.title, buf);
    lv_label_set_text(O.sub, subtitle ? subtitle : "");
    lv_obj_set_style_border_color(O.card, lv_color_hex(k_kind[kind].col), 0);
    lv_obj_clear_flag(O.card, LV_OBJ_FLAG_HIDDEN);
    O.kind = kind; O.visible = true;
    if (duration_ms) { lv_timer_set_period(O.hide_timer, duration_ms); lv_timer_reset(O.hide_timer); lv_timer_resume(O.hide_timer); }
    else lv_timer_pause(O.hide_timer);
    dsk_lvgl_unlock();
}

void dsk_overlay_hide(void)
{
    if (!O.card || !dsk_lvgl_lock(300)) return;
    O.visible = false; lv_obj_add_flag(O.card, LV_OBJ_FLAG_HIDDEN); lv_timer_pause(O.hide_timer);
    dsk_lvgl_unlock();
}
bool dsk_overlay_visible(void) { return O.visible; }
dsk_overlay_kind_t dsk_overlay_kind(void) { return O.kind; }

void dsk_overlay_set_status(const dsk_status_t *st)
{
    if (!O.icon_bar || !dsk_lvgl_lock(100)) return;
    lv_obj_set_style_text_color(O.ic_wifi, lv_color_hex(st->wifi ? 0x34D399 : 0x475569), 0);
    lv_obj_set_style_text_color(O.ic_mac, lv_color_hex(st->mac ? 0x34D399 : (st->pairing ? 0xFBBF24 : 0xFF4D4D)), 0);
    lv_obj_set_style_text_color(O.ic_ble, lv_color_hex(st->ble ? 0x3FA9FF : 0x475569), 0);
    dsk_lvgl_unlock();
}

void dsk_overlay_set_clock(const char *hhmm)
{
    if (!O.clock || !dsk_lvgl_lock(100)) return;
    lv_label_set_text(O.clock, hhmm ? hhmm : "");
    dsk_lvgl_unlock();
}
