#include <cmath>
#include <cstring>
#include <string>

#include "board_config.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "secrets.h"

static const char *TAG = "crypto_monitor";

static constexpr uint8_t SYMBOL_COUNT = 3;
static constexpr const char *BINANCE_API = "https://api.binance.com/api/v3/ticker/price?symbol=";
static constexpr int32_t PRICE_RANGE = 200;
static constexpr uint32_t POINTS_TO_CHART = 15;
static constexpr uint32_t UPDATE_UI_INTERVAL_MS = 1000;

static const char *symbols[SYMBOL_COUNT] = {"BTCUSDT", "ETHUSDT", "BCHUSDT"};
static float prices[SYMBOL_COUNT] = {0.0f};
static float open_prices[SYMBOL_COUNT] = {0.0f};
static uint8_t symbol_index_to_chart = 0;
static int32_t max_range = 0;
static int32_t min_range = 0;

static lv_display_t *display = nullptr;
static lv_obj_t *chart = nullptr;
static lv_chart_series_t *data_series = nullptr;
static lv_obj_t *price_box[SYMBOL_COUNT];
static lv_obj_t *price_title[SYMBOL_COUNT];
static lv_obj_t *price_label[SYMBOL_COUNT];
static lv_obj_t *label_footer = nullptr;

static esp_lcd_panel_handle_t panel_handle = nullptr;
static SemaphoreHandle_t lvgl_mutex = nullptr;
static EventGroupHandle_t wifi_events = nullptr;

static constexpr EventBits_t WIFI_CONNECTED_BIT = BIT0;

struct HttpBuffer {
    std::string body;
};

static bool lock_lvgl(TickType_t timeout_ticks = pdMS_TO_TICKS(1000)) {
    return xSemaphoreTake(lvgl_mutex, timeout_ticks) == pdTRUE;
}

static void unlock_lvgl() {
    xSemaphoreGive(lvgl_mutex);
}

static void set_display_brightness(uint8_t brightness) {
    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode = LCD_BL_LEDC_MODE;
    timer_cfg.timer_num = LCD_BL_LEDC_TIMER;
    timer_cfg.duty_resolution = LEDC_TIMER_8_BIT;
    timer_cfg.freq_hz = 1000;
    timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t channel_cfg = {};
    channel_cfg.gpio_num = LCD_PIN_BL;
    channel_cfg.speed_mode = LCD_BL_LEDC_MODE;
    channel_cfg.channel = LCD_BL_LEDC_CHANNEL;
    channel_cfg.intr_type = LEDC_INTR_DISABLE;
    channel_cfg.timer_sel = LCD_BL_LEDC_TIMER;
    channel_cfg.duty = brightness;
    channel_cfg.hpoint = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));
    ESP_ERROR_CHECK(ledc_set_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL, brightness));
    ESP_ERROR_CHECK(ledc_update_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL));
}

static void lv_tick_cb(void *arg) {
    LV_UNUSED(arg);
    lv_tick_inc(2);
}

static void lv_log_cb(lv_log_level_t level, const char *buf) {
    LV_UNUSED(level);
    ESP_LOGI(TAG, "%s", buf);
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static void init_display_and_lvgl() {
    spi_bus_config_t bus_cfg = {};
    bus_cfg.sclk_io_num = LCD_PIN_SCLK;
    bus_cfg.mosi_io_num = LCD_PIN_MOSI;
    bus_cfg.miso_io_num = GPIO_NUM_NC;
    bus_cfg.quadwp_io_num = GPIO_NUM_NC;
    bus_cfg.quadhd_io_num = GPIO_NUM_NC;
    bus_cfg.max_transfer_sz = LCD_H_RES * 40 * sizeof(lv_color_t);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.cs_gpio_num = LCD_PIN_CS;
    io_cfg.dc_gpio_num = LCD_PIN_DC;
    io_cfg.spi_mode = 0;
    io_cfg.pclk_hz = LCD_SPI_CLOCK_HZ;
    io_cfg.trans_queue_depth = 10;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;

    esp_lcd_panel_io_handle_t panel_io = nullptr;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &panel_io));

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = LCD_PIN_RST;
    panel_cfg.color_space = ESP_LCD_COLOR_SPACE_RGB;
    panel_cfg.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_cfg, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, LCD_GAP_X, LCD_GAP_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, LCD_SWAP_XY));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, LCD_MIRROR_X, LCD_MIRROR_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, LCD_INVERT_COLOR));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    set_display_brightness(SCREEN_BRIGHTNESS);

    lv_init();
#if LV_USE_LOG != 0
    lv_log_register_print_cb(lv_log_cb);
#endif

    size_t buffer_pixels = LCD_H_RES * 40;
    auto *draw_buf = static_cast<lv_color_t *>(heap_caps_malloc(buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (draw_buf == nullptr) {
        draw_buf = static_cast<lv_color_t *>(heap_caps_malloc(buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_8BIT));
    }
    ESP_ERROR_CHECK(draw_buf ? ESP_OK : ESP_ERR_NO_MEM);

    display = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_flush_cb(display, lvgl_flush_cb);
    lv_display_set_buffers(display, draw_buf, nullptr, buffer_pixels * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);

    const esp_timer_create_args_t tick_timer_args = {
        .callback = &lv_tick_cb,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lv_tick",
        .skip_unhandled_events = false,
    };
    esp_timer_handle_t tick_timer = nullptr;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 2000));
}

static esp_err_t http_event_cb(esp_http_client_event_t *evt) {
    auto *buffer = static_cast<HttpBuffer *>(evt->user_data);
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0 && buffer != nullptr) {
        buffer->body.append(static_cast<const char *>(evt->data), evt->data_len);
    }
    return ESP_OK;
}

static bool http_get(const char *url, std::string &out_body) {
    HttpBuffer buffer;

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 3000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = http_event_cb;
    config.user_data = &buffer;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status == 200) {
        out_body = std::move(buffer.body);
        return true;
    }
    return false;
}

static bool parse_price(const std::string &body, float &out) {
    const std::string key = "\"price\":\"";
    const size_t start = body.find(key);
    if (start == std::string::npos) {
        return false;
    }
    const size_t value_start = start + key.size();
    const size_t value_end = body.find('"', value_start);
    if (value_end == std::string::npos || value_end <= value_start) {
        return false;
    }
    out = strtof(body.substr(value_start, value_end - value_start).c_str(), nullptr);
    return true;
}

static void fetch_prices() {
    for (uint8_t i = 0; i < SYMBOL_COUNT; ++i) {
        std::string url = std::string(BINANCE_API) + symbols[i];
        std::string body;
        float fetched = prices[i];

        ESP_LOGI(TAG, "[API] GET %s", url.c_str());
        if (http_get(url.c_str(), body) && parse_price(body, fetched)) {
            prices[i] = fetched;
            if (open_prices[i] <= 0.0f) {
                open_prices[i] = fetched;
            }
            ESP_LOGI(TAG, "[API] OK  -> %s %.2f", symbols[i], fetched);
        } else {
            ESP_LOGW(TAG, "[API] ERR -> %s", symbols[i]);
        }
    }
}

static void update_ui() {
    if (chart == nullptr || data_series == nullptr) {
        return;
    }

    int32_t p = static_cast<int32_t>(lroundf(prices[symbol_index_to_chart] * 100.0f));
    lv_chart_set_next_value(chart, data_series, p);

    if (p < min_range) {
        min_range = p;
    }
    if (p > max_range) {
        max_range = p;
    }
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, min_range, max_range);

    for (uint8_t i = 0; i < SYMBOL_COUNT; ++i) {
        float pct = 0.0f;
        if (open_prices[i] > 0.0f) {
            pct = (prices[i] - open_prices[i]) / open_prices[i] * 100.0f;
        }

        lv_label_set_text_fmt(price_label[i],
                              "%.2f  %c%.2f%%",
                              prices[i],
                              pct >= 0.0f ? '+' : '-', fabsf(pct));

        lv_obj_set_style_text_color(price_label[i],
                                    pct >= 0.0f ? lv_palette_lighten(LV_PALETTE_GREEN, 4)
                                                : lv_palette_lighten(LV_PALETTE_RED, 3),
                                    0);

        lv_color_t bg = pct >= 0.0f
                            ? lv_color_mix(lv_palette_main(LV_PALETTE_GREEN), lv_color_black(), 127)
                            : lv_color_mix(lv_palette_main(LV_PALETTE_RED), lv_color_black(), 127);
        lv_obj_set_style_bg_color(price_box[i], bg, 0);
        lv_obj_set_height(price_box[i], LV_SIZE_CONTENT);
    }

    lv_label_set_text_fmt(label_footer,
                          "RAM %u KB",
                          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024));
}

static void build_ui() {
    lv_obj_clean(lv_screen_active());

    chart = lv_chart_create(lv_screen_active());
    lv_chart_set_point_count(chart, POINTS_TO_CHART);
    lv_obj_set_size(chart, 160, 80);
    lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 24);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);

    int32_t p = static_cast<int32_t>(lroundf(open_prices[symbol_index_to_chart] * 100.0f));
    max_range = p + PRICE_RANGE;
    min_range = p - PRICE_RANGE;
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, min_range, max_range);

    data_series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *chart_title = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(chart_title, &lv_font_montserrat_18, 0);
    lv_label_set_text_fmt(chart_title, "%s Chart", symbols[symbol_index_to_chart]);
    lv_obj_set_style_text_color(chart_title, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_align_to(chart_title, chart, LV_ALIGN_OUT_TOP_LEFT, 0, -4);

    for (uint8_t i = 0; i < SYMBOL_COUNT; ++i) {
        price_box[i] = lv_obj_create(lv_screen_active());
        lv_obj_set_size(price_box[i], LV_PCT(100), LV_SIZE_CONTENT);

        if (i == 0) {
            lv_obj_align(price_box[i], LV_ALIGN_TOP_LEFT, 0, 112);
        } else {
            lv_obj_align_to(price_box[i], price_box[i - 1], LV_ALIGN_OUT_BOTTOM_LEFT, 0, 3);
        }

        lv_obj_set_style_radius(price_box[i], 6, 0);
        lv_obj_set_style_pad_all(price_box[i], 4, 0);

        price_title[i] = lv_label_create(price_box[i]);
        lv_obj_set_style_text_font(price_title[i], &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(price_title[i], lv_color_white(), 0);
        lv_label_set_text(price_title[i], symbols[i]);
        lv_obj_align(price_title[i], LV_ALIGN_TOP_LEFT, 0, 0);

        price_label[i] = lv_label_create(price_box[i]);
        lv_obj_set_style_text_font(price_label[i], &lv_font_montserrat_16, 0);
        lv_obj_align_to(price_label[i], price_title[i], LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
        lv_label_set_text(price_label[i], "--");
    }

    label_footer = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(label_footer, &lv_font_montserrat_16, 0);
    lv_obj_align(label_footer, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_label_set_text(label_footer, "RAM -- KB");
    lv_obj_set_style_text_color(label_footer, lv_palette_main(LV_PALETTE_CYAN), 0);
}

static void wifi_event_cb(void *arg,
                          esp_event_base_t event_base,
                          int32_t event_id,
                          void *event_data) {
    LV_UNUSED(arg);
    LV_UNUSED(event_data);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void init_wifi_sta() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_cb, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_cb, nullptr));

    wifi_config_t wifi_config = {};
    std::strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid), WIFI_SSID, sizeof(wifi_config.sta.ssid));
    std::strncpy(reinterpret_cast<char *>(wifi_config.sta.password), WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void ui_task(void *arg) {
    LV_UNUSED(arg);
    while (true) {
        if (lock_lvgl(pdMS_TO_TICKS(100))) {
            lv_timer_handler();
            unlock_lvgl();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void price_task(void *arg) {
    LV_UNUSED(arg);
    while (true) {
        fetch_prices();
        if (lock_lvgl()) {
            update_ui();
            unlock_lvgl();
        }
        vTaskDelay(pdMS_TO_TICKS(UPDATE_UI_INTERVAL_MS));
    }
}

extern "C" void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    lvgl_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(lvgl_mutex ? ESP_OK : ESP_ERR_NO_MEM);

    wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(wifi_events ? ESP_OK : ESP_ERR_NO_MEM);

    init_display_and_lvgl();

    if (lock_lvgl()) {
        lv_obj_t *spinner = lv_spinner_create(lv_screen_active());
        lv_spinner_set_anim_params(spinner, 8000, 200);
        lv_obj_set_size(spinner, 80, 80);
        lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);

        lv_obj_t *label = lv_label_create(lv_screen_active());
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(label, lv_color_hex3(0x0cf), 0);
        lv_obj_set_width(label, 160);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text_fmt(label, "Connecting to\n%s", WIFI_SSID);
        lv_obj_align_to(label, spinner, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
        unlock_lvgl();
    }

    init_wifi_sta();

    while ((xEventGroupGetBits(wifi_events) & WIFI_CONNECTED_BIT) == 0) {
        if (lock_lvgl(pdMS_TO_TICKS(50))) {
            lv_timer_handler();
            unlock_lvgl();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    fetch_prices();

    if (lock_lvgl()) {
        build_ui();
        update_ui();
        unlock_lvgl();
    }

    xTaskCreate(ui_task, "ui_task", 4096, nullptr, 4, nullptr);
    xTaskCreate(price_task, "price_task", 8192, nullptr, 4, nullptr);
}
