#pragma once

// Screen colors
#define LCD_COLOR_FOREGROUND ORANGE
#define LCD_COLOR_BACKGROUND BLACK

// Enable fullscreen mode
#define LCD_FULLSCREEN

// Custom title above the emulated LCD area.
// Leave undefined for true fullscreen.
#define LCD_TITLE "NoTouchFw v1.3.1"

// Mirror screen horizontally
//#define LCD_MIRROR_HORIZONTALLY

// Mirror screen vertically
//#define LCD_MIRROR_VERTICALLY

// Rotate screen by 180°
//#define LCD_ROTATE_180
#if defined(LCD_ROTATE_180)
    #define LCD_MIRROR_HORIZONTALLY
    #define LCD_MIRROR_VERTICALLY
#endif

// LCD backlight idle timeout in seconds.
// Leave undefined to disable idle off.
//#define LCD_IDLE_TIMEOUT_SEC 5

// Enable LCD backlight being controlled by PWM
#if !defined(MKS_32_V1_4) && !defined(MKS_28_V1_0)
    #define LCD_PWM_DIMMER
#endif
#if defined(LCD_PWM_DIMMER)
    #define LCD_LED_PWM_ON_BRIGHTNESS 100
    #define LCD_LED_PWM_OFF_BRIGHTNESS 2
#endif

// Enable SPI data received indicator in the title bar
//#define SPI_DATA_RECEIVED_INDICATOR

// Rotary knob long press duration in seconds for SPI restart.
// Leave undefined to disable the reset cycle.
#define SPI_RESTART_KNOB_PRESS_DURATION_SEC 3

// Knob GRB color.
// Leave undefined to disable knob RGB led.
//#define KNOB_RGB_COLOR 0xFFFFFF
