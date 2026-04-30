#include "display_grid.h"
#include "ui_fonts.h"
#include <Arduino.h>

#define GRID_WEEKS   53
#define GRID_DAYS    7
#define SCREEN_W     320
#define SCREEN_H     172
#define CELL_W       5
#define CELL_H       12
#define COL_GAP      1
#define ROW_GAP      2
#define FOOTER_H     28
#define LEFT_PAD     8
#define LABEL_FONT_H 18
#define LABEL_GAP    6
#define GRID_PIXEL_H ((GRID_DAYS * CELL_H) + ((GRID_DAYS - 1) * ROW_GAP))
#define GRID_UNIT_H  (LABEL_FONT_H + LABEL_GAP + GRID_PIXEL_H)
#define UNIT_TOP     ((SCREEN_H - FOOTER_H - GRID_UNIT_H) / 2)
#define GRID_TOP     (UNIT_TOP + LABEL_FONT_H + LABEL_GAP)
#define MONTH_LABEL_COUNT 6

// Colour table: level 0-4 (normal and bright/peak for animation)
static const lv_color_t LEVEL_COLORS[5] = {
    lv_color_hex(0x161b22),
    lv_color_hex(0x2b8a4f),
    lv_color_hex(0x48c765),
    lv_color_hex(0x56d364),
    lv_color_hex(0x7ee787),
};
static const lv_color_t LEVEL_BRIGHT[5] = {
    lv_color_hex(0x161b22), // level 0 never animates
    lv_color_hex(0x48b86b),
    lv_color_hex(0x6fe08a),
    lv_color_hex(0x8ef0a4),
    lv_color_hex(0xa8ffb5),
};

static lv_obj_t *s_screen = nullptr;
static lv_obj_t *s_cells[GRID_WEEKS][GRID_DAYS];
static lv_obj_t *s_month_labels[MONTH_LABEL_COUNT];

// Store level per cell for animation callback lookup
static uint8_t s_cell_levels[GRID_WEEKS][GRID_DAYS];

// Animation callback: val 0-255, interpolates base->bright
static void anim_color_cb(void *obj, int32_t val) {
    lv_obj_t *cell = (lv_obj_t *)obj;
    uint8_t lvl = (uint8_t)(uintptr_t)lv_obj_get_user_data(cell);
    if (lvl >= 5) lvl = 4;
    lv_color_t mixed = lv_color_mix(LEVEL_BRIGHT[lvl], LEVEL_COLORS[lvl], (uint8_t)val);
    lv_obj_set_style_bg_color(cell, mixed, 0);
}

static void civil_from_days(int32_t days_since_epoch, int &year, int &month, int &day) {
    int32_t z = days_since_epoch + 719468;
    const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint32_t doe = (uint32_t)(z - era * 146097);
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    year = (int)(yoe + era * 400);
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint32_t mp = (5 * doy + 2) / 153;
    day = (int)(doy - (153 * mp + 2) / 5 + 1);
    month = (int)(mp + (mp < 10 ? 3 : -9));
    year += (month <= 2);
}

static const char *month_abbr(int month) {
    static const char *MONTHS[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    if (month < 1 || month > 12) return "";
    return MONTHS[month - 1];
}

static void update_month_labels(const GithubData &data) {
    static const uint8_t label_weeks[MONTH_LABEL_COUNT] = {0, 9, 18, 27, 36, 45};
    static const char *fallback_months[MONTH_LABEL_COUNT] = {"Apr", "Jun", "Aug", "Oct", "Dec", "Feb"};

    for (int i = 0; i < MONTH_LABEL_COUNT; i++) {
        const char *text = fallback_months[i];
        if (data.anchor_week_start_days != 0) {
            int32_t week_start_days = data.anchor_week_start_days - (GRID_WEEKS - 1 - label_weeks[i]) * 7;
            int year = 0;
            int month = 0;
            int day = 0;
            civil_from_days(week_start_days + 3, year, month, day);
            text = month_abbr(month);
        }

        lv_label_set_text(s_month_labels[i], text);
        int x = LEFT_PAD + label_weeks[i] * (CELL_W + COL_GAP);
        lv_obj_set_pos(s_month_labels[i], x, UNIT_TOP);
    }
}

lv_obj_t *display_grid_build(const char *username) {
    memset(s_cells, 0, sizeof(s_cells));
    memset(s_cell_levels, 0, sizeof(s_cell_levels));
    memset(s_month_labels, 0, sizeof(s_month_labels));

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    const lv_color_t label_color = lv_color_white();

    for (int i = 0; i < MONTH_LABEL_COUNT; i++) {
        lv_obj_t *lbl = lv_label_create(s_screen);
        lv_obj_set_style_text_font(lbl, ui_font_label(), 0);
        lv_obj_set_style_text_color(lbl, label_color, 0);
        lv_label_set_text(lbl, "");
        lv_obj_set_pos(lbl, LEFT_PAD, UNIT_TOP);
        s_month_labels[i] = lbl;
    }

    update_month_labels(GithubData{});

    // Grid cells: 53 columns x 7 rows
    for (int w = 0; w < GRID_WEEKS; w++) {
        for (int d = 0; d < GRID_DAYS; d++) {
            lv_obj_t *cell = lv_obj_create(s_screen);
            lv_obj_set_size(cell, CELL_W, CELL_H);
            lv_obj_set_style_radius(cell, 1, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_set_style_bg_color(cell, LEVEL_COLORS[0], 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_user_data(cell, (void *)(uintptr_t)0);
            int x = LEFT_PAD + w * (CELL_W + COL_GAP);
            int y = GRID_TOP  + d * (CELL_H + ROW_GAP);
            lv_obj_set_pos(cell, x, y);
            s_cells[w][d] = cell;
            s_cell_levels[w][d] = 0;
        }
    }

    // Footer container
    lv_obj_t *footer = lv_obj_create(s_screen);
    lv_obj_set_size(footer, SCREEN_W, FOOTER_H);
    lv_obj_set_pos(footer, 0, SCREEN_H - FOOTER_H);
    lv_obj_set_style_bg_color(footer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    // Username label - left
    lv_obj_t *user_lbl = lv_label_create(footer);
    lv_obj_set_style_text_font(user_lbl, ui_font_label(), 0);
    lv_obj_set_style_text_color(user_lbl, label_color, 0);
    char buf[80];
    snprintf(buf, sizeof(buf), "github.com/%s", username);
    lv_label_set_text(user_lbl, buf);
    lv_obj_align(user_lbl, LV_ALIGN_LEFT_MID, LEFT_PAD, 0);

    // Legend: 5 squares right-aligned, then "More" label
    // Build right-to-left: "More" label, then squares
    lv_obj_t *more_lbl = lv_label_create(footer);
    lv_obj_set_style_text_font(more_lbl, ui_font_label(), 0);
    lv_obj_set_style_text_color(more_lbl, label_color, 0);
    lv_label_set_text(more_lbl, "More");
    lv_obj_align(more_lbl, LV_ALIGN_RIGHT_MID, -LEFT_PAD, 0);

    // Place squares left of "More" label
    // Each square is 5px wide + 2px gap
    for (int i = 4; i >= 0; i--) {
        lv_obj_t *sq = lv_obj_create(footer);
        lv_obj_set_size(sq, CELL_W, CELL_H);
        lv_obj_set_style_radius(sq, 1, 0);
        lv_obj_set_style_border_width(sq, 0, 0);
        lv_obj_set_style_pad_all(sq, 0, 0);
        lv_obj_set_style_bg_color(sq, LEVEL_COLORS[i], 0);
        lv_obj_set_style_bg_opa(sq, LV_OPA_COVER, 0);
        // Position: right edge of "More" text minus offset per square
        // "More" text ~24px wide, right edge at 320-8=312, so More starts at ~288
        // squares at: 288 - 2(gap) - 5(sq) = 281, then 281-7=274, etc.
        int x_right = SCREEN_W - LEFT_PAD - 40; // approximate left edge of the larger "More" label
        int sq_x = x_right - (4 - i + 1) * (CELL_W + 2);
        lv_obj_set_pos(sq, sq_x, (FOOTER_H - CELL_H) / 2);
    }

    return s_screen;
}

void display_grid_stop_animations() {
    if (!s_screen) return;

    for (int w = 0; w < GRID_WEEKS; w++) {
        for (int d = 0; d < GRID_DAYS; d++) {
            if (s_cells[w][d]) {
                lv_anim_delete(s_cells[w][d], anim_color_cb);
                // Restore base colour
                uint8_t lvl = s_cell_levels[w][d];
                lv_obj_set_style_bg_color(s_cells[w][d], LEVEL_COLORS[lvl], 0);
            }
        }
    }
}

void display_grid_update(const GithubData &data, const Config &cfg) {
    if (!s_screen) return;

    display_grid_stop_animations();
    update_month_labels(data);

    // Collect non-zero counts to compute percentile threshold
    uint16_t counts[GRID_WEEKS * GRID_DAYS];
    int count_n = 0;
    for (int w = 0; w < (int)data.week_count; w++) {
        for (int d = 0; d < GRID_DAYS; d++) {
            if (data.days[w][d].count > 0)
                counts[count_n++] = data.days[w][d].count;
        }
    }

    // Sort ascending (insertion sort - max 371 items)
    for (int i = 1; i < count_n; i++) {
        uint16_t key = counts[i];
        int j = i - 1;
        while (j >= 0 && counts[j] > key) { counts[j+1] = counts[j]; j--; }
        counts[j+1] = key;
    }

    // Threshold: value at (100 - anim_top_pct)th percentile
    uint16_t threshold = 0xFFFF;
    if (count_n > 0 && cfg.anim_top_pct > 0) {
        int idx = (int)(count_n * (100 - cfg.anim_top_pct) / 100.0f);
        if (idx >= count_n) idx = count_n - 1;
        threshold = counts[idx];
        if (threshold == 0) threshold = 1; // never animate zero-count cells
    }

    const int week_count = (data.week_count > GRID_WEEKS) ? GRID_WEEKS : data.week_count;

    // Update cells and assign animations.
    for (int display_w = 0; display_w < GRID_WEEKS; display_w++) {
        for (int d = 0; d < GRID_DAYS; d++) {
            uint16_t cnt = (display_w < week_count) ? data.days[display_w][d].count : 0;
            uint8_t  lvl = (display_w < week_count) ? data.days[display_w][d].level : 0;
            s_cell_levels[display_w][d] = lvl;

            lv_obj_set_user_data(s_cells[display_w][d], (void *)(uintptr_t)lvl);
            lv_obj_set_style_bg_color(s_cells[display_w][d], LEVEL_COLORS[lvl], 0);

            if (cnt > 0 && cnt >= threshold) {
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, s_cells[display_w][d]);
                lv_anim_set_exec_cb(&a, anim_color_cb);
                lv_anim_set_values(&a, 0, 255);
                lv_anim_set_duration(&a, cfg.anim_period_ms);
                lv_anim_set_playback_duration(&a, cfg.anim_period_ms);
                lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
                lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
                lv_anim_set_delay(&a, (uint32_t)random(0, (long)cfg.anim_period_ms));
                lv_anim_start(&a);
            }
        }
    }
}
