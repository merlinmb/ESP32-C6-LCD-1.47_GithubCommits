#include "ui_fonts.h"

#include "fonts/smooth_font_data.h"

#if LV_USE_TINY_TTF
#include <src/libs/tiny_ttf/lv_tiny_ttf.h>
#endif

static lv_font_t *s_label_font = nullptr;
static lv_font_t *s_stat_font = nullptr;
static bool s_init_attempted = false;

bool ui_fonts_init() {
    if (s_init_attempted) return (s_label_font != nullptr && s_stat_font != nullptr);
    s_init_attempted = true;

#if LV_USE_TINY_TTF
    if (g_smooth_font_size > 0) {
        s_label_font = lv_tiny_ttf_create_data_ex(g_smooth_font_data, g_smooth_font_size,
                                                  18, LV_FONT_KERNING_NORMAL, 96);
        s_stat_font = lv_tiny_ttf_create_data_ex(g_smooth_font_data, g_smooth_font_size,
                                                 34, LV_FONT_KERNING_NORMAL, 128);
    }
#endif

    return (s_label_font != nullptr && s_stat_font != nullptr);
}

const lv_font_t *ui_font_label() {
    return s_label_font ? s_label_font : &lv_font_montserrat_16;
}

const lv_font_t *ui_font_stat() {
    return s_stat_font ? s_stat_font : &lv_font_montserrat_32;
}