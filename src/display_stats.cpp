#include "display_stats.h"
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

    // Username top-left
    lv_obj_t *user_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_font(user_lbl, &lv_font_montserrat_8, 0);
    lv_obj_set_style_text_color(user_lbl, lv_color_hex(0x8b949e), 0);
    char buf[80];
    snprintf(buf, sizeof(buf), "github.com/%s", username);
    lv_label_set_text(user_lbl, buf);
    lv_obj_align(user_lbl, LV_ALIGN_TOP_LEFT, 8, 6);

    // Three stat cards side by side
    // Card layout: each ~106px wide, vertically centred on screen
    struct CardDef {
        lv_obj_t **val_ptr;
        const char *label;
        lv_color_t color;
    };
    CardDef cards[3] = {
        { &s_streak_val,  "day streak",    lv_color_hex(0x39d353) },
        { &s_total_val,   "contributions", lv_color_hex(0x58a6ff) },
        { &s_busiest_val, "busiest day",   lv_color_hex(0xd29922) },
    };

    const int card_w = 106;
    const int screen_w = 320;
    const int screen_h = 172;

    for (int i = 0; i < 3; i++) {
        // Centre of each card in x
        int cx = (screen_w / 6) + i * (screen_w / 3);
        int cy = screen_h / 2 + 6; // slight downward offset from centre

        // Value label (large)
        *cards[i].val_ptr = lv_label_create(s_screen);
        lv_obj_set_style_text_font(*cards[i].val_ptr, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(*cards[i].val_ptr, cards[i].color, 0);
        lv_label_set_text(*cards[i].val_ptr, "--");
        lv_obj_align(*cards[i].val_ptr, LV_ALIGN_TOP_LEFT, cx - card_w/2, cy - 28);

        // Sub-label
        lv_obj_t *sub = lv_label_create(s_screen);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_8, 0);
        lv_obj_set_style_text_color(sub, lv_color_hex(0x8b949e), 0);
        lv_label_set_text(sub, cards[i].label);
        lv_obj_align_to(sub, *cards[i].val_ptr, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
    }

    // Age label bottom-right
    s_age_label = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_age_label, &lv_font_montserrat_8, 0);
    lv_obj_set_style_text_color(s_age_label, lv_color_hex(0x8b949e), 0);
    lv_label_set_text(s_age_label, "");
    lv_obj_align(s_age_label, LV_ALIGN_BOTTOM_RIGHT, -8, -6);

    return s_screen;
}

void display_stats_update(const GithubData &data) {
    if (!s_screen) return;
    lv_label_set_text_fmt(s_streak_val,  "%d", (int)data.current_streak);
    lv_label_set_text_fmt(s_total_val,   "%d", (int)data.total_contributions);
    lv_label_set_text_fmt(s_busiest_val, "%d", (int)data.busiest_day_count);
}

void display_stats_set_age(uint32_t minutes_ago) {
    if (!s_age_label) return;
    if (minutes_ago == 0)
        lv_label_set_text(s_age_label, "just updated");
    else
        lv_label_set_text_fmt(s_age_label, "updated %dm ago", (int)minutes_ago);
}
