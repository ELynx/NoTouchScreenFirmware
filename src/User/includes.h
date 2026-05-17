#ifndef _INCLUDES_H_
#define _INCLUDES_H_

#include "variants.h"

#define STRINGIFY_(M) #M
#define STRINGIFY(M) STRINGIFY_(M)
#define FIRMWARE_NAME STRINGIFY(HARDWARE) "." STRINGIFY(SOFTWARE_VERSION)

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "features.h"
#include "delay.h"

#include "Colors.h"
#include "lcd.h"
#include "LCD_Init.h"
#include "GUI.h"

#include "CircularQueue.h"
#include "spi_slave.h"
#include "encoder.h"
#include "knobled.h"

#ifndef ST7920_SPI
  #error "No ST7920"
#endif
#if defined(LCD_PWM_DIMMER)
  #if !defined(LCD_LED_PIN)
    #error "LCD_PWM_DIMMER requires LCD_LED_PIN"
  #endif
  #if !defined(LCD_LED_PIN_ALTERNATE)
    #error "LCD_PWM_DIMMER requires LCD_LED_PIN_ALTERNATE"
  #endif
  #if !defined(LCD_LED_PWM_CHANNEL)
    #error "LCD_PWM_DIMMER requires LCD_LED_PWM_CHANNEL"
  #endif
  #if !defined(LCD_LED_PWM_ON_BRIGHTNESS)
    #define LCD_LED_PWM_ON_BRIGHTNESS 100
  #elif LCD_LED_PWM_ON_BRIGHTNESS > 100
    #define LCD_LED_PWM_ON_BRIGHTNESS 100
  #endif
  #if !defined(LCD_LED_PWM_OFF_BRIGHTNESS)
    #define LCD_LED_PWM_OFF_BRIGHTNESS 20
  #elif LCD_LED_PWM_OFF_BRIGHTNESS < 0
    #define LCD_LED_PWM_OFF_BRIGHTNESS 0
  #endif
  #if LCD_LED_PWM_ON_BRIGHTNESS <= LCD_LED_PWM_OFF_BRIGHTNESS
    #error "LCD_LED_PWM_ON_BRIGHTNESS needs to be greater than LCD_LED_PWM_OFF_BRIGHTNESS"
  #endif
#endif
#if defined(LCD_IDLE_TIMEOUT_SEC)
  #if !LCD_ENCODER_SUPPORT
    #error "LCD_IDLE_TIMEOUT_SEC requires encoder"
  #endif
  #if !defined(LCD_LED_PIN)
    #error "LCD_IDLE_TIMEOUT_SEC requires LCD_LED_PIN"
  #endif
#endif
#if defined(SPI_RESTART_KNOB_PRESS_DURATION_SEC)
  #if !LCD_ENCODER_SUPPORT
    #error "SPI_RESTART_KNOB_PRESS_DURATION_SEC requires encoder"
  #endif
#endif
#if defined(LCD_SD_TEXT_FILE) && defined(LCD_TITLE)
  #error "LCD_TITLE cannot be defined when LCD_SD_TEXT_FILE is enabled"
#endif
#if defined(LCD_SD_TEXT_FILE) && !defined(SD_SPI_SUPPORT)
  #error "LCD_SD_TEXT_FILE requires SD_SPI_SUPPORT"
#endif
#if defined(LCD_SD_LOGO_FOLDER) && !defined(SD_SPI_SUPPORT)
  #error "LCD_SD_LOGO_FOLDER requires SD_SPI_SUPPORT"
#endif
#if defined(SPI_DATA_RECEIVED_INDICATOR) && defined(LCD_SD_TEXT_FILE)
  #error "SPI_DATA_RECEIVED_INDICATOR cannot be combined with LCD_SD_TEXT_FILE"
#endif
#if defined(SPI_DATA_RECEIVED_INDICATOR) && !defined(LCD_TITLE)
  #error "SPI_DATA_RECEIVED_INDICATOR requires LCD_TITLE"
#endif
#if defined(KNOB_RGB_COLOR)
  #if !defined(KNOB_LED_COLOR_PIN)
    #error "KNOB_RGB_COLOR requires KNOB_LED_COLOR_PIN"
  #endif
  #if !defined(NEOPIXEL_PIXELS)
    #error "KNOB_RGB_COLOR requires NEOPIXEL_PIXELS"
  #endif
  #if !defined(WS2812_FAST_WRITE_HIGH)
    #error "KNOB_RGB_COLOR requires WS2812_FAST_WRITE_HIGH"
  #endif
  #if !defined(WS2812_FAST_WRITE_LOW)
    #error "KNOB_RGB_COLOR requires WS2812_FAST_WRITE_LOW"
  #endif
#endif
#endif
