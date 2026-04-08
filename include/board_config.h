#pragma once

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"

#define SCREEN_BRIGHTNESS 255

#define LCD_HOST SPI2_HOST
#define LCD_H_RES 172
#define LCD_V_RES 320
#define LCD_SPI_CLOCK_HZ (40 * 1000 * 1000)

#define LCD_PIN_SCLK GPIO_NUM_6
#define LCD_PIN_MOSI GPIO_NUM_7
#define LCD_PIN_CS GPIO_NUM_14
#define LCD_PIN_DC GPIO_NUM_15
#define LCD_PIN_RST GPIO_NUM_21
#define LCD_PIN_BL GPIO_NUM_22

#define LCD_GAP_X 34
#define LCD_GAP_Y 0
#define LCD_SWAP_XY 0
#define LCD_MIRROR_X 0
#define LCD_MIRROR_Y 0
#define LCD_INVERT_COLOR 1

#define LCD_BL_LEDC_MODE LEDC_LOW_SPEED_MODE
#define LCD_BL_LEDC_TIMER LEDC_TIMER_0
#define LCD_BL_LEDC_CHANNEL LEDC_CHANNEL_0
#define LCD_BL_LEDC_DUTY_MAX 255
