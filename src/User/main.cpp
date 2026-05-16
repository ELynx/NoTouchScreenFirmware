#include "includes.h"

#include "myfatfs.h"
#include "ff.h"
#include "timer.h"
#include "GPIO_Init.h"

#define private public
#include "St7920Emulator.hpp"
#undef private

#define ST7920_GXROWS 128.0
#define ST7920_GYROWS 64.0
#define LCD_TEXT_CHAR_WIDTH 8
#define LCD_TEXT_CHAR_STEP 8
#define LCD_TEXT_FONT_HEIGHT 16
#define LCD_TEXT_LINE_HEIGHT 16
#define LCD_TITLE_BAR_HEIGHT (LCD_TEXT_FONT_HEIGHT + 1)
#ifndef LCD_SD_TEXT_FILE
  #define LCD_SD_TEXT_FILE "Psalter.txt"
#endif
#ifndef LCD_SD_TEXT_DELAY_SEC
  #define LCD_SD_TEXT_DELAY_SEC 5UL
#endif
#define LCD_SD_TEXT_RETRY_MS 10000UL
#define LCD_SD_TEXT_MAX_CHARS 128
#if defined(LCD_SD_TEXT_OVERLAY)
  #define LCD_EMULATOR_TOP_MARGIN LCD_TITLE_BAR_HEIGHT
  #define LCD_EMULATOR_BOTTOM_MARGIN LCD_TEXT_LINE_HEIGHT
#elif defined(LCD_TITLE)
  #define LCD_EMULATOR_TOP_MARGIN LCD_TITLE_BAR_HEIGHT
  #define LCD_EMULATOR_BOTTOM_MARGIN 0
#else
  #define LCD_EMULATOR_TOP_MARGIN 0
  #define LCD_EMULATOR_BOTTOM_MARGIN 0
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
#if defined(KNOB_RGB_COLOR) || defined(LCD_ENCODER_POLLING) || defined(LCD_SD_TEXT_OVERLAY)
  #define LCD_TIMER_TICK
#endif

static uint8_t normalizeTextChar(char c) {
  uint8_t glyphChar = (uint8_t)c;
  if (glyphChar == 0 || glyphChar > 0x7F) {
    return '?';
  }
  return glyphChar;
}
static uint16_t textWidth(const char *text) {
  uint16_t width = 0;
  while (*text != '\0' && width < LCD_WIDTH) {
    width += LCD_TEXT_CHAR_STEP;
    ++text;
  }
  return width > LCD_WIDTH ? LCD_WIDTH : width;
}
static uint16_t centeredTextX(const char *text) {
  uint16_t width = textWidth(text);
  if (LCD_WIDTH <= width) {
    return 0;
  }
  return (LCD_WIDTH - width) / 2;
}
static void drawTextLine(const char *text, uint16_t x, uint16_t y) {
  while (*text != '\0' && x + LCD_TEXT_CHAR_WIDTH <= LCD_WIDTH) {
    uint16_t charIndex = ((uint16_t)normalizeTextChar(*text) - 1) * LCD_TEXT_FONT_HEIGHT;
    for (uint8_t row = 0; row < LCD_TEXT_FONT_HEIGHT; ++row) {
      uint8_t rowBits = St7920Emulator::pFont816[charIndex + row];
      for (uint8_t col = 0; col < LCD_TEXT_CHAR_WIDTH; ++col) {
        if ((rowBits & (1 << col)) > 0) {
          FILLRECT(x + col, y + row, 1, 1, LCD_COLOR_FOREGROUND);
        }
      }
    }
    x += LCD_TEXT_CHAR_STEP;
    ++text;
  }
}
#if defined(LCD_TITLE) || defined(LCD_SD_TEXT_OVERLAY)
static void drawTitleBarText(const char *text) {
  uint16_t titleX = centeredTextX(text);
  uint16_t titleWidth = textWidth(text);
  uint16_t titleEnd = titleX + titleWidth;

  if (titleWidth == 0) {
    FILLRECT(0, LCD_TITLE_BAR_HEIGHT - 1, LCD_WIDTH, 1, LCD_COLOR_FOREGROUND);
    return;
  }

  drawTextLine(text, titleX, 0);

  if (titleX > 1) {
    FILLRECT(0, LCD_TITLE_BAR_HEIGHT - 1, titleX - 1, 1, LCD_COLOR_FOREGROUND);
  }
  if (titleEnd < LCD_WIDTH) {
    FILLRECT(titleEnd, LCD_TITLE_BAR_HEIGHT - 1, LCD_WIDTH - titleEnd, 1, LCD_COLOR_FOREGROUND);
  }
}
#else
static inline void drawTitleBarText(const char *text) { (void)text; }
#endif

#if defined(LCD_SD_TEXT_OVERLAY)
static bool msElapsed(uint32_t currentMs, uint32_t targetMs) {
  return ((int32_t)(currentMs - targetMs)) >= 0;
}

static uint16_t st7920BottomY(void) {
  lcd_pixel_type bottom = st7920StartY + st7920PixelSize * ST7920_GYROWS;
  if (bottom >= LCD_HEIGHT) {
    return LCD_HEIGHT;
  }
  return (uint16_t)(bottom + 0.999f);
}

static void clearTextBand(uint16_t y) {
  uint16_t h = LCD_TEXT_LINE_HEIGHT;
  if (y + h > LCD_HEIGHT) {
    h = LCD_HEIGHT - y;
  }
  if (h > 0) {
    uint16_t sy = _Y(y, h);
    GUI_FillRectColor(0, sy, LCD_WIDTH, sy + h, LCD_COLOR_BACKGROUND);
  }
}

static bool bottomTextY(uint16_t *y) {
  uint16_t bottom = st7920BottomY();
  if (bottom + LCD_TEXT_FONT_HEIGHT > LCD_HEIGHT) {
    return false;
  }

  uint16_t margin = LCD_HEIGHT - bottom;
  *y = bottom + (margin - LCD_TEXT_FONT_HEIGHT) / 2;
  return true;
}

static FIL sdTextFile;
static bool bSdTextFileOpen = false;
static bool bSdTextTitleCached = false;
static FSIZE_t uiSdTextScrollStart = 0;
static uint32_t ui32SdTextNextMs = 0;
static char pSdTextTitle[LCD_SD_TEXT_MAX_CHARS];
static char pSdTextLine[LCD_SD_TEXT_MAX_CHARS];

static void closeSdTextFile(void) {
  if (bSdTextFileOpen) {
    f_close(&sdTextFile);
    bSdTextFileOpen = false;
  }
}

static bool readSdTextLine(char *line, uint16_t lineSize) {
  if (lineSize < 2) {
    return false;
  }

  uint16_t pos = 0;
  bool readAny = false;

  while (true) {
    char c;
    UINT br = 0;
    FRESULT result = f_read(&sdTextFile, &c, 1, &br);
    if (result != FR_OK) {
      return false;
    }
    if (br != 1) {
      break;
    }

    readAny = true;
    if (c == '\n') {
      break;
    }
    if (c == '\r') {
      continue;
    }
    if (c == '\t' || (uint8_t)c < ' ') {
      c = ' ';
    }
    if (pos < lineSize - 1) {
      line[pos++] = c;
    }
  }

  line[pos] = '\0';
  return readAny;
}

static bool seekSdTextScrollStart(void) {
  if (f_lseek(&sdTextFile, uiSdTextScrollStart) != FR_OK) {
    closeSdTextFile();
    return false;
  }
  return true;
}

static bool prepareSdTextFile(bool bSdMounted) {
  if (!bSdMounted) {
    return false;
  }

  if (!bSdTextFileOpen) {
    bSdTextFileOpen = (f_open(&sdTextFile, LCD_SD_TEXT_FILE, FA_OPEN_EXISTING | FA_READ) == FR_OK);
    if (!bSdTextFileOpen) {
      return false;
    }

    if (bSdTextTitleCached) {
      return seekSdTextScrollStart();
    }
  }

  if (!bSdTextTitleCached) {
    if (!readSdTextLine(pSdTextTitle, sizeof(pSdTextTitle))) {
      pSdTextTitle[0] = '\0';
      closeSdTextFile();
      return false;
    }
    uiSdTextScrollStart = f_tell(&sdTextFile);
    bSdTextTitleCached = true;
  }

  return true;
}

static bool initSdTextOverlay(bool bSdMounted) {
  pSdTextTitle[0] = '\0';
  uiSdTextScrollStart = 0;
  bSdTextTitleCached = false;
  return prepareSdTextFile(bSdMounted);
}

static bool nextSdTextLine(char *line, uint16_t lineSize) {
  if (readSdTextLine(line, lineSize)) {
    return true;
  }

  if (!seekSdTextScrollStart()) {
    return false;
  }
  return readSdTextLine(line, lineSize);
}

static void updateSdTextOverlay(bool bSdMounted, uint32_t currentMs) {
  if (!msElapsed(currentMs, ui32SdTextNextMs)) {
    return;
  }

  uint16_t y;
  if (!bottomTextY(&y)) {
    ui32SdTextNextMs = currentMs + LCD_SD_TEXT_RETRY_MS;
    return;
  }

  bool bTitleWasCached = bSdTextTitleCached;
  if (!prepareSdTextFile(bSdMounted)) {
    clearTextBand(y);
    closeSdTextFile();
    ui32SdTextNextMs = currentMs + LCD_SD_TEXT_RETRY_MS;
    return;
  }
  if (!bTitleWasCached) {
    drawTitleBarText(pSdTextTitle);
  }

  if (!nextSdTextLine(pSdTextLine, sizeof(pSdTextLine))) {
    clearTextBand(y);
    closeSdTextFile();
    ui32SdTextNextMs = currentMs + LCD_SD_TEXT_RETRY_MS;
    return;
  }

  clearTextBand(y);
  drawTextLine(pSdTextLine, 0, y);
  ui32SdTextNextMs = currentMs + LCD_SD_TEXT_DELAY_SEC * 1000UL;
}
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
  bool bSdMounted = mountSDCard();
  if(bSdMounted)
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
  st7920PixelSize = min(LCD_WIDTH / ST7920_GXROWS, (LCD_HEIGHT - LCD_EMULATOR_TOP_MARGIN - LCD_EMULATOR_BOTTOM_MARGIN) / ST7920_GYROWS);
  st7920StartX = (LCD_WIDTH - st7920PixelSize * ST7920_GXROWS) / 2;
  st7920StartY = LCD_EMULATOR_TOP_MARGIN + (LCD_HEIGHT - LCD_EMULATOR_TOP_MARGIN - LCD_EMULATOR_BOTTOM_MARGIN - st7920PixelSize * ST7920_GYROWS) / 2;

#if defined(LCD_SD_TEXT_OVERLAY)
  initSdTextOverlay(bSdMounted);
  drawTitleBarText(pSdTextTitle);
#elif defined(LCD_TITLE)
  drawTitleBarText(LCD_TITLE);
#endif

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
  uint16_t ui16TitleX = centeredTextX(LCD_TITLE);
  uint16_t ui16TitleEnd = ui16TitleX + textWidth(LCD_TITLE);
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

#if defined(LCD_SD_TEXT_OVERLAY)
    updateSdTextOverlay(bSdMounted, Timer_GetTimerMs());
#endif
  }
}
