#include "includes.h"

#include "myfatfs.h"
#include "ff.h"
#include "timer.h"
#include "GPIO_Init.h"

#include "St7920Emulator.hpp"

#define ST7920_GXROWS 128.0
#define ST7920_GYROWS 64.0
#define LCD_TITLE_BAR_HEIGHT 8
#define LCD_TITLE_CHAR_WIDTH 5
#define LCD_TITLE_CHAR_STEP 6
#if defined(LCD_TITLE)
  #define LCD_EMULATOR_TOP_MARGIN LCD_TITLE_BAR_HEIGHT
#else
  #define LCD_EMULATOR_TOP_MARGIN 0
#endif
#if defined(LCD_FULLSCREEN)
  typedef float lcd_pixel_type;
#else
  typedef uint16_t lcd_pixel_type;
#endif
lcd_pixel_type st7920PixelSize;
lcd_pixel_type st7920StartX;
lcd_pixel_type st7920StartY;
static inline lcd_pixel_type min(lcd_pixel_type a, lcd_pixel_type b) {
  if (a < b) {
    return a;
  }
  return b;
}
#if defined(LCD_MIRROR_HORIZONTALLY)
  #define _X(X,W) (LCD_WIDTH - (X) - (W) - 1)
#else
  #define _X(X,W) (X)
#endif
#if defined(LCD_MIRROR_VERTICALLY)
  #define _Y(Y,H) (LCD_HEIGHT - (Y) - (H) - 1)
#else
  #define _Y(Y,H) (Y)
#endif
#define FILLRECT(X,Y,W,H,C) \
  GUI_FillRectColor(_X(X,W), _Y(Y,H), _X(X,W) + W, _Y(Y,H) + H, C);

#if LCD_ENCODER_SUPPORT && (defined(SPI_RESTART_KNOB_PRESS_DURATION_SEC) || defined(LCD_IDLE_TIMEOUT_SEC))
  #define LCD_ENCODER_POLLING
#endif
#if defined(KNOB_RGB_COLOR) || defined(LCD_ENCODER_POLLING)
  #define LCD_TIMER_TICK
#endif

#if defined(LCD_TITLE)
static void getTitleGlyph(char c, uint8_t glyph[5]) {
  #define SET_GLYPH(A,B,C,D,E) do { glyph[0] = (A); glyph[1] = (B); glyph[2] = (C); glyph[3] = (D); glyph[4] = (E); return; } while (0)

  switch (c) {
    case ' ': SET_GLYPH(0x00, 0x00, 0x00, 0x00, 0x00);
    case '!': SET_GLYPH(0x00, 0x00, 0x5F, 0x00, 0x00);
    case '.': SET_GLYPH(0x00, 0x60, 0x60, 0x00, 0x00);
    case '-': SET_GLYPH(0x08, 0x08, 0x08, 0x08, 0x08);
    case '_': SET_GLYPH(0x40, 0x40, 0x40, 0x40, 0x40);
    case ':': SET_GLYPH(0x00, 0x36, 0x36, 0x00, 0x00);
    case '/': SET_GLYPH(0x20, 0x10, 0x08, 0x04, 0x02);
    case '+': SET_GLYPH(0x08, 0x08, 0x3E, 0x08, 0x08);

    case '0': SET_GLYPH(0x3E, 0x51, 0x49, 0x45, 0x3E);
    case '1': SET_GLYPH(0x00, 0x42, 0x7F, 0x40, 0x00);
    case '2': SET_GLYPH(0x42, 0x61, 0x51, 0x49, 0x46);
    case '3': SET_GLYPH(0x22, 0x49, 0x49, 0x49, 0x36);
    case '4': SET_GLYPH(0x18, 0x14, 0x12, 0x7F, 0x10);
    case '5': SET_GLYPH(0x27, 0x45, 0x45, 0x45, 0x39);
    case '6': SET_GLYPH(0x3C, 0x4A, 0x49, 0x49, 0x30);
    case '7': SET_GLYPH(0x01, 0x71, 0x09, 0x05, 0x03);
    case '8': SET_GLYPH(0x36, 0x49, 0x49, 0x49, 0x36);
    case '9': SET_GLYPH(0x06, 0x49, 0x49, 0x29, 0x1E);

    case 'A': SET_GLYPH(0x7E, 0x11, 0x11, 0x11, 0x7E);
    case 'B': SET_GLYPH(0x7F, 0x49, 0x49, 0x49, 0x36);
    case 'C': SET_GLYPH(0x3E, 0x41, 0x41, 0x41, 0x22);
    case 'D': SET_GLYPH(0x7F, 0x41, 0x41, 0x22, 0x1C);
    case 'E': SET_GLYPH(0x7F, 0x49, 0x49, 0x49, 0x41);
    case 'F': SET_GLYPH(0x7F, 0x09, 0x09, 0x09, 0x01);
    case 'G': SET_GLYPH(0x3E, 0x41, 0x49, 0x49, 0x7A);
    case 'H': SET_GLYPH(0x7F, 0x08, 0x08, 0x08, 0x7F);
    case 'I': SET_GLYPH(0x00, 0x41, 0x7F, 0x41, 0x00);
    case 'J': SET_GLYPH(0x20, 0x40, 0x41, 0x3F, 0x01);
    case 'K': SET_GLYPH(0x7F, 0x08, 0x14, 0x22, 0x41);
    case 'L': SET_GLYPH(0x7F, 0x40, 0x40, 0x40, 0x40);
    case 'M': SET_GLYPH(0x7F, 0x02, 0x0C, 0x02, 0x7F);
    case 'N': SET_GLYPH(0x7F, 0x02, 0x04, 0x08, 0x7F);
    case 'O': SET_GLYPH(0x3E, 0x41, 0x41, 0x41, 0x3E);
    case 'P': SET_GLYPH(0x7F, 0x09, 0x09, 0x09, 0x06);
    case 'Q': SET_GLYPH(0x3E, 0x41, 0x51, 0x21, 0x5E);
    case 'R': SET_GLYPH(0x7F, 0x09, 0x19, 0x29, 0x46);
    case 'S': SET_GLYPH(0x46, 0x49, 0x49, 0x49, 0x31);
    case 'T': SET_GLYPH(0x01, 0x01, 0x7F, 0x01, 0x01);
    case 'U': SET_GLYPH(0x3F, 0x40, 0x40, 0x40, 0x3F);
    case 'V': SET_GLYPH(0x1F, 0x20, 0x40, 0x20, 0x1F);
    case 'W': SET_GLYPH(0x3F, 0x40, 0x38, 0x40, 0x3F);
    case 'X': SET_GLYPH(0x63, 0x14, 0x08, 0x14, 0x63);
    case 'Y': SET_GLYPH(0x07, 0x08, 0x70, 0x08, 0x07);
    case 'Z': SET_GLYPH(0x61, 0x51, 0x49, 0x45, 0x43);

    case 'a': SET_GLYPH(0x20, 0x54, 0x54, 0x54, 0x78);
    case 'b': SET_GLYPH(0x7F, 0x48, 0x44, 0x44, 0x38);
    case 'c': SET_GLYPH(0x38, 0x44, 0x44, 0x44, 0x28);
    case 'd': SET_GLYPH(0x38, 0x44, 0x44, 0x48, 0x7F);
    case 'e': SET_GLYPH(0x38, 0x54, 0x54, 0x54, 0x18);
    case 'f': SET_GLYPH(0x08, 0x7E, 0x09, 0x01, 0x02);
    case 'g': SET_GLYPH(0x08, 0x14, 0x54, 0x54, 0x3C);
    case 'h': SET_GLYPH(0x7F, 0x04, 0x04, 0x78, 0x00);
    case 'i': SET_GLYPH(0x00, 0x44, 0x7D, 0x40, 0x00);
    case 'j': SET_GLYPH(0x20, 0x40, 0x44, 0x3D, 0x00);
    case 'k': SET_GLYPH(0x7F, 0x10, 0x28, 0x44, 0x00);
    case 'l': SET_GLYPH(0x00, 0x41, 0x7F, 0x40, 0x00);
    case 'm': SET_GLYPH(0x7C, 0x04, 0x18, 0x04, 0x78);
    case 'n': SET_GLYPH(0x7C, 0x08, 0x04, 0x04, 0x78);
    case 'o': SET_GLYPH(0x38, 0x44, 0x44, 0x44, 0x38);
    case 'p': SET_GLYPH(0x7C, 0x14, 0x14, 0x14, 0x08);
    case 'q': SET_GLYPH(0x08, 0x14, 0x14, 0x18, 0x7C);
    case 'r': SET_GLYPH(0x7C, 0x08, 0x04, 0x04, 0x08);
    case 's': SET_GLYPH(0x48, 0x54, 0x54, 0x54, 0x20);
    case 't': SET_GLYPH(0x04, 0x3F, 0x44, 0x40, 0x20);
    case 'u': SET_GLYPH(0x3C, 0x40, 0x20, 0x7C, 0x00);
    case 'v': SET_GLYPH(0x1F, 0x20, 0x40, 0x20, 0x1F);
    case 'w': SET_GLYPH(0x3C, 0x60, 0x30, 0x60, 0x3C);
    case 'x': SET_GLYPH(0x44, 0x28, 0x10, 0x28, 0x44);
    case 'y': SET_GLYPH(0x0C, 0x50, 0x50, 0x50, 0x3C);
    case 'z': SET_GLYPH(0x44, 0x64, 0x54, 0x4C, 0x44);
  }

  SET_GLYPH(0x02, 0x01, 0x51, 0x09, 0x06);
  #undef SET_GLYPH
}
static uint16_t titleTextWidth(const char *text) {
  uint16_t len = 0;
  while (text[len] != '\0') {
    ++len;
  }

  uint32_t width = (uint32_t)len * LCD_TITLE_CHAR_STEP;
  if (width > LCD_WIDTH) {
    return LCD_WIDTH;
  }
  return width;
}
static uint16_t titleStartX(const char *text) {
  uint16_t width = titleTextWidth(text);
  if (LCD_WIDTH <= width) {
    return 0;
  }
  return (LCD_WIDTH - width) / 2;
}
static void drawTitleText(const char *text, uint16_t x, uint16_t y) {
  uint8_t glyph[LCD_TITLE_CHAR_WIDTH];

  while (*text != '\0' && x + LCD_TITLE_CHAR_WIDTH <= LCD_WIDTH) {
    getTitleGlyph(*text, glyph);
    for (uint8_t col = 0; col < LCD_TITLE_CHAR_WIDTH; ++col) {
      for (uint8_t row = 0; row < 7; ++row) {
        if ((glyph[col] & (1 << row)) > 0) {
          FILLRECT(x + col, y + row, 1, 1, LCD_COLOR_FOREGROUND);
        }
      }
    }
    x += LCD_TITLE_CHAR_STEP;
    ++text;
  }
}
static void drawTitleBar(void) {
  uint16_t titleX = titleStartX(LCD_TITLE);
  uint16_t titleWidth = titleTextWidth(LCD_TITLE);
  uint16_t titleEnd = titleX + titleWidth;

  drawTitleText(LCD_TITLE, titleX, 0);

  if (titleX > 1) {
    FILLRECT(0, LCD_TITLE_BAR_HEIGHT - 1, titleX - 1, 1, LCD_COLOR_FOREGROUND);
  }
  if (titleEnd < LCD_WIDTH) {
    FILLRECT(titleEnd, LCD_TITLE_BAR_HEIGHT - 1, LCD_WIDTH - titleEnd, 1, LCD_COLOR_FOREGROUND);
  }
}
#else
static inline void drawTitleBar(void) {}
#endif

void clearDisplay() {
  // Clear ST7920 gui rect
  FILLRECT(st7920StartX,
           st7920StartY,
           st7920PixelSize * ST7920_GXROWS,
           st7920PixelSize * ST7920_GYROWS,
           LCD_COLOR_BACKGROUND);
}
void drawByte(uint8_t x, uint8_t y, uint8_t d) {
  // Loop over all bits
  for (uint8_t i = 0; i < 8; ++i, ++x) {
    // Draw pixel
    FILLRECT(st7920StartX + x * st7920PixelSize,
             st7920StartY + y * st7920PixelSize,
             st7920PixelSize,
             st7920PixelSize,
             ((d & (1 << i)) > 0) ? LCD_COLOR_FOREGROUND : LCD_COLOR_BACKGROUND);
  }
}

extern volatile uint32_t ui32SpiActivated;

int main(void)
{
  SystemClockInit();

  // Set vector table offset
  SCB->VTOR = VECT_TAB_FLASH;

  // Get clock frequency
  RCC_ClocksTypeDef rccClocks;
  RCC_GetClocksFreq(&rccClocks);

  // Init NVIC priority group
#if defined(GD32F2XX)
  GD_NVIC_PriorityGroupConfig();
#else
  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
#endif

  // Init delay
  Delay_init(rccClocks.HCLK_Frequency);

  // Disable JTAG
  #ifdef DISABLE_JTAG
    #if defined(GD32F2XX)
      DISABLE_JTAG();
    #else
      RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
      GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE); // disable JTAG, enable SWD
    #endif
  #endif

  // Disable SWJ
  #ifdef DISABLE_DEBUG
    #if !defined(GD32F2XX)
      RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
      GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE); //disable JTAG & SWD
    #endif
  #endif

  // Mount SD card
  if(mountSDCard())
  {
    // Check if firmware binary exists
    if (f_file_exists(FIRMWARE_NAME ".bin"))
    {
      // Check of old firmware binary exists
      if (f_file_exists(FIRMWARE_NAME ".CUR"))
      {
        // Delete old firmware binary
        f_unlink(FIRMWARE_NAME ".CUR");
      }

      // Rename current firmware binary
      f_rename(FIRMWARE_NAME ".bin", FIRMWARE_NAME ".CUR");
    }
  }

  // Init PS_On
#if defined(PS_ON_PIN)
  GPIO_InitSet(PS_ON_PIN, MGPIO_MODE_OUT_PP, 0);
  GPIO_SetLevel(PS_ON_PIN, 1);
#endif

  // Init LCD
  LCD_Init(&rccClocks, LCD_COLOR_BACKGROUND);

  // Init timer tick
#if defined(LCD_TIMER_TICK)
  Timer_Init(&rccClocks);
#endif

  // Init knob LED
#if defined(KNOB_RGB_COLOR)
  KnobLed_Init(rccClocks.PCLK1_Frequency);
#endif

  // Calculate ST7920 screen dimensions
  st7920PixelSize = min(LCD_WIDTH / ST7920_GXROWS, (LCD_HEIGHT - LCD_EMULATOR_TOP_MARGIN) / ST7920_GYROWS);
  st7920StartX = (LCD_WIDTH - st7920PixelSize * ST7920_GXROWS) / 2;
  st7920StartY = LCD_EMULATOR_TOP_MARGIN + (LCD_HEIGHT - LCD_EMULATOR_TOP_MARGIN - st7920PixelSize * ST7920_GYROWS) / 2;

  drawTitleBar();

  // Create emulator handle
  St7920Emulator st7920Emulator(clearDisplay, drawByte);

  // Show startup message
  const uint8_t pStartupMessage[] = {0xF8, 0x30, 0x00, 0x00, 0x60, 0x00, 0xC0, 0xF8, 0x80, 0x00, 0xFA, 0x50, 0x30, 0x50, 0x40, 0x30, 0x70, 0x30, 0x90, 0x30, 0x20, 0x30, 0x00, 0x40, 0x50, 0x60, 0xD0, 0x70, 0x50, 0x60, 0xC0, 0x60, 0x10, 0x70, 0x40, 0x60, 0xF0, 0x70, 0x20, 0xF8, 0x90, 0x00, 0xFA, 0x70, 0x20, 0x60, 0x50, 0x60, 0x10, 0x60, 0x40, 0x70, 0x90};
  for (uint8_t i = 0; i < sizeof(pStartupMessage); ++i) {
    st7920Emulator.parseSerialData(pStartupMessage[i]);
  }
  st7920Emulator.reset(false);

  // Init slave SPI
  ui32SpiActivated = 0;
  CIRCULAR_QUEUE spiQueue;
  SPI_Slave(&spiQueue);

#if defined(LCD_ENCODER_POLLING)
  Encoder_Init();
#endif

  // Variables for SPI restart by long knob press
#if defined(LCD_ENCODER_POLLING) && defined(SPI_RESTART_KNOB_PRESS_DURATION_SEC)
  uint32_t ui32FirstBtnPress = 0;
#endif

  // Variables for lcd idle off
#if defined(LCD_ENCODER_POLLING) && defined(LCD_IDLE_TIMEOUT_SEC)
  bool bScreenOn = true;
  uint8_t ui8LastEncoder = 0;
  uint32_t ui32LastActive = 0;
#endif

  // Variables for SPI data received indicator
#if defined(SPI_DATA_RECEIVED_INDICATOR) && defined(LCD_TITLE)
  uint16_t ui16TitleX = titleStartX(LCD_TITLE);
  uint16_t ui16TitleEnd = ui16TitleX + titleTextWidth(LCD_TITLE);
  bool bSpiDataIndicator = ui16TitleX > 2;
  bool bSpiActivityIndicator = (ui16TitleEnd + 10) < LCD_WIDTH;
  uint16_t ui16DX = 0, ui16DY = 0, ui16AX = ui16TitleEnd, ui16AY = 0;
  uint16_t ui16DColor = LCD_COLOR_FOREGROUND, ui16AColor = LCD_COLOR_FOREGROUND;
  uint32_t ui32LastSpiActivated = 0;
#endif

  // Endless loop
  uint8_t data;
  while(true) {
    // Check if SPI data is available
    if (SPI_SlaveGetData(&data)) {
      // Parse data
      st7920Emulator.parseSerialData(data);

      // Update SPI data received indicator
#if defined(SPI_DATA_RECEIVED_INDICATOR) && defined(LCD_TITLE)
      if (bSpiDataIndicator) {
        // Draw new pixel
        FILLRECT(ui16DX, ui16DY, 1, 1, ui16DColor);

        // Move to next pixel
        if (ui16DX < ui16TitleX - 2) {
          ++ui16DX;
        } else {
          ui16DX = 0;

          // Wrap to next line
          if (ui16DY < 6) {
            ++ui16DY;
          } else {
            ui16DY = 0;

            // Invert color
            if (ui16DColor == LCD_COLOR_FOREGROUND) {
              ui16DColor = LCD_COLOR_BACKGROUND;
            } else {
              ui16DColor = LCD_COLOR_FOREGROUND;
            }
          }
        }
      }
#endif
    }

    // Update SPI activation display
#if defined(SPI_DATA_RECEIVED_INDICATOR) && defined(LCD_TITLE)
    if (bSpiActivityIndicator) {
      while (ui32LastSpiActivated < ui32SpiActivated) {
        // Draw new pixel
        FILLRECT(ui16AX, ui16AY, 10, 1, ui16AColor);

        // Move to next pixel
        if (ui16AX < (LCD_WIDTH - 11)) {
          ui16AX += 10;
        } else {
          ui16AX = ui16TitleEnd;

          // Wrap to next line
          if (ui16AY < 6) {
            ++ui16AY;
          } else {
            ui16AY = 0;

            // Invert color
            if (ui16AColor == LCD_COLOR_FOREGROUND) {
              ui16AColor = LCD_COLOR_BACKGROUND;
            } else {
              ui16AColor = LCD_COLOR_FOREGROUND;
            }
          }
        }

        // Update spi activated count
        ++ui32LastSpiActivated;
      }
    } else {
      ui32LastSpiActivated = ui32SpiActivated;
    }
#endif

#if defined(LCD_ENCODER_POLLING)
    // Read current encoder value
    uint8_t ui8CurrentEncoder = Encoder_Read();

    // Get current time
    uint32_t ui32CurrentMs = Timer_GetTimerMs();

#if defined(SPI_RESTART_KNOB_PRESS_DURATION_SEC)
    // Check if encoder button is pressed
    if ((ui8CurrentEncoder & LCD_ENCODER_BTN_SET) > 0) {
      // Check if we need to store the current timestamp
      if (ui32FirstBtnPress == 0) {
        // Store current timestamp
        if (ui32CurrentMs == 0) {
          ui32FirstBtnPress = 1;
        } else {
          ui32FirstBtnPress = ui32CurrentMs;
        }
      }
    } else if (ui32FirstBtnPress > 0) {
      // Get difference to button press timestamp
      uint32_t ui32BtnPressDuration;
      if (ui32CurrentMs >= ui32FirstBtnPress) {
        ui32BtnPressDuration = ui32CurrentMs - ui32FirstBtnPress;
      } else {
        ui32BtnPressDuration = 0xFFFFFFFF - ui32FirstBtnPress + ui32CurrentMs + 1;
      }

      if (ui32BtnPressDuration >= SPI_RESTART_KNOB_PRESS_DURATION_SEC * 1000UL) {
        // Turn off backlight
        #ifdef LCD_LED_PIN
          LCD_LED_Off();
        #endif

        // Reset SPI
        SPI_SlaveDeinit();

        // Wait 100ms
        Delay_ms(100);

        // Init SPI
        SPI_Slave(&spiQueue);

        // Turn on backlight
        #ifdef LCD_LED_PIN
          LCD_LED_On();
        #endif

        // Turn on knob LED
        #ifdef KNOB_RGB_COLOR
          KnobLed_On();
        #endif

        // Reset emulator
        st7920Emulator.reset(true);
      }

      // Clear flag
      ui32FirstBtnPress = 0;
    }
#endif // SPI_RESTART_KNOB_PRESS_DURATION_SEC

    // Check if lcd idle off is enabled
#if defined(LCD_IDLE_TIMEOUT_SEC)
    // Compare to last value
    if (ui8CurrentEncoder != ui8LastEncoder) {
      // Store current value
      ui8LastEncoder = ui8CurrentEncoder;

      // Check if screen is off
      if (!bScreenOn) {
        // Turn on screen
        LCD_LED_On();

        // Turn on knob LED
        #ifdef KNOB_RGB_COLOR
          KnobLed_On();
        #endif

        // Set flag
        bScreenOn = true;
      }

      // Store current time
      ui32LastActive = ui32CurrentMs;
    }
    // Check if screen is on
    else if (bScreenOn) {
      // Get difference to last active timestamp
      uint32_t ui32IdleDuration;
      if (ui32CurrentMs >= ui32LastActive) {
        ui32IdleDuration = ui32CurrentMs - ui32LastActive;
      } else {
        ui32IdleDuration = 0xFFFFFFFF - ui32LastActive + ui32CurrentMs + 1;
      }

      // Check inactivity time
      if (ui32IdleDuration >= LCD_IDLE_TIMEOUT_SEC * 1000UL) {
        // Turn off screen
        LCD_LED_Off();

        // Turn off knob LED
        #ifdef KNOB_RGB_COLOR
          KnobLed_Off();
        #endif

        // Clear flag
        bScreenOn = false;
      }
    }
#endif // LCD_IDLE_TIMEOUT_SEC

#endif // LCD_ENCODER_POLLING
  }
}
