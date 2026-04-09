#include "display_stats.h"
#include "ui_fonts.h"
#include <Arduino.h>

static lv_obj_t *s_screen      = nullptr;
static lv_obj_t *s_streak_val  = nullptr;
static lv_obj_t *s_total_val   = nullptr;
static lv_obj_t *s_busiest_val = nullptr;
static lv_obj_t *s_age_label   = nullptr;

lv_obj_t *display_stats_build(const char *username) {
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    const lv_color_t label_color = lv_color_white();
    const int screen_w = 320;
    const int card_w = 92;
    const int card_gap = 10;
    const int total_cards_w = (card_w * 3) + (card_gap * 2);
    const int cards_left = (screen_w - total_cards_w) / 2;
    const int value_y = 54;

    // Username centered at the top
    lv_obj_t *user_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_font(user_lbl, ui_font_label(), 0);
    lv_obj_set_style_text_color(user_lbl, label_color, 0);
    lv_obj_set_style_text_align(user_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(user_lbl, screen_w - 24);
    char buf[80];
    snprintf(buf, sizeof(buf), "github.com/%s", username);
    lv_label_set_text(user_lbl, buf);
    lv_obj_align(user_lbl, LV_ALIGN_TOP_MID, 0, 12);

    // Three centered stat columns
    struct CardDef {
        lv_obj_t **val_ptr;
        const char *label;
        lv_color_t color;
    };
    CardDef cards[3] = {
        { &s_streak_val,  "day\nstreak",    lv_color_hex(0x39d353) },
        { &s_total_val,   "Contrib.",      lv_color_hex(0x58a6ff) },
        { &s_busiest_val, "busiest\nday",   lv_color_hex(0xd29922) },
    };

    for (int i = 0; i < 3; i++) {
        int card_x = cards_left + i * (card_w + card_gap);

        *cards[i].val_ptr = lv_label_create(s_screen);
        lv_obj_set_style_text_font(*cards[i].val_ptr, ui_font_stat(), 0);
        lv_obj_set_style_text_color(*cards[i].val_ptr, cards[i].color, 0);
        lv_obj_set_style_text_align(*cards[i].val_ptr, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(*cards[i].val_ptr, card_w);
        lv_label_set_text(*cards[i].val_ptr, "--");
        lv_obj_set_pos(*cards[i].val_ptr, card_x, value_y);

        lv_obj_t *sub = lv_label_create(s_screen);
        lv_obj_set_style_text_font(sub, ui_font_label(), 0);
        lv_obj_set_style_text_color(sub, label_color, 0);
        lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(sub, card_w);
        lv_label_set_text(sub, cards[i].label);
        lv_obj_align_to(sub, *cards[i].val_ptr, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
    }

    // Age label centered at the bottom
    s_age_label = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_age_label, ui_font_label(), 0);
    lv_obj_set_style_text_color(s_age_label, label_color, 0);
    lv_obj_set_style_text_align(s_age_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_age_label, screen_w - 24);
    lv_label_set_text(s_age_label, "");
    lv_obj_align(s_age_label, LV_ALIGN_BOTTOM_MID, 0, -12);

    return s_screen;
}

void display_stats_update(const GithubData &data) {
    if (!s_screen || !s_streak_val || !s_total_val || !s_busiest_val) return;
    lv_label_set_text_fmt(s_streak_val,  "%d", (int)data.current_streak);
    lv_label_set_text_fmt(s_total_val,   "%d", (int)data.total_contributions);
    if (data.busiest_day_day > 0 && data.busiest_day_month > 0)
        lv_label_set_text_fmt(s_busiest_val, "%02u/%02u",
                              (unsigned)data.busiest_day_day,
                              (unsigned)data.busiest_day_month);
    else
        lv_label_set_text(s_busiest_val, "--/--");
}

void display_stats_set_age(uint32_t minutes_ago) {
    if (!s_age_label) return;
    if (minutes_ago == 0)
        lv_label_set_text(s_age_label, "just updated");
    else
        lv_label_set_text_fmt(s_age_label, "updated %dm ago", (int)minutes_ago);
}
