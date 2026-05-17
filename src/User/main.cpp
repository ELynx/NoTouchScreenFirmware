#include "includes.h"

#include "myfatfs.h"
#include "ff.h"
#include "timer.h"
#include "GPIO_Init.h"

#include <math.h>

#define private public
#include "St7920Emulator.hpp"
#undef private

#define ST7920_GXROWS 128.0
#define ST7920_GYROWS 64.0
#ifndef ST7920_ASPECT_WIDTH
  #define ST7920_ASPECT_WIDTH ST7920_GXROWS
#endif
#ifndef ST7920_ASPECT_HEIGHT
  #define ST7920_ASPECT_HEIGHT ST7920_GYROWS
#endif
#define LCD_TEXT_CHAR_WIDTH 8
#define LCD_TEXT_CHAR_STEP 8
#define LCD_TEXT_FONT_HEIGHT 16
#define LCD_TEXT_LINE_HEIGHT 16
#define LCD_TITLE_BAR_HEIGHT LCD_TEXT_FONT_HEIGHT
#ifndef LCD_SD_TEXT_DELAY_SEC
  #define LCD_SD_TEXT_DELAY_SEC 5
#endif
#if defined(LCD_SD_LOGO_FOLDER) && !defined(LCD_SD_LOGO_DELAY_MS)
  #define LCD_SD_LOGO_DELAY_MS 1000
#endif
#define LCD_SD_TEXT_RETRY_MS 10000
#define LCD_SD_TEXT_MAX_CHARS 128
#if defined(LCD_SD_TEXT_FILE)
  #define LCD_EMULATOR_TOP_MARGIN LCD_TITLE_BAR_HEIGHT
  #define LCD_EMULATOR_BOTTOM_MARGIN LCD_TEXT_LINE_HEIGHT
#elif defined(LCD_TITLE)
  #define LCD_EMULATOR_TOP_MARGIN LCD_TITLE_BAR_HEIGHT
  #define LCD_EMULATOR_BOTTOM_MARGIN 0
#else
  #define LCD_EMULATOR_TOP_MARGIN 0
  #define LCD_EMULATOR_BOTTOM_MARGIN 0
#endif
typedef float lcd_pixel_type;

static lcd_pixel_type st7920Width;
static lcd_pixel_type st7920Height;
static lcd_pixel_type st7920PixelWidth;
static lcd_pixel_type st7920PixelHeight;
static lcd_pixel_type st7920StartX;
static lcd_pixel_type st7920StartY;

static inline lcd_pixel_type minLcdPixel(lcd_pixel_type a, lcd_pixel_type b) {
  return a < b ? a : b;
}

static inline uint16_t clampDisplayCoordinate(lcd_pixel_type value, uint16_t limit) {
  if (value <= 0) {
    return 0;
  }
  if (value >= limit) {
    return limit;
  }
  return (uint16_t)(value + 0.5f);
}

static void fillRect(lcd_pixel_type x,
                     lcd_pixel_type y,
                     lcd_pixel_type w,
                     lcd_pixel_type h,
                     uint16_t color) {
  if (w <= 0 || h <= 0) {
    return;
  }

#if defined(LCD_MIRROR_HORIZONTALLY)
  const lcd_pixel_type sx = (lcd_pixel_type)LCD_WIDTH - x - w;
  const lcd_pixel_type ex = (lcd_pixel_type)LCD_WIDTH - x;
#else
  const lcd_pixel_type sx = x;
  const lcd_pixel_type ex = x + w;
#endif

#if defined(LCD_MIRROR_VERTICALLY)
  const lcd_pixel_type sy = (lcd_pixel_type)LCD_HEIGHT - y - h;
  const lcd_pixel_type ey = (lcd_pixel_type)LCD_HEIGHT - y;
#else
  const lcd_pixel_type sy = y;
  const lcd_pixel_type ey = y + h;
#endif

  const uint16_t x0 = clampDisplayCoordinate(sx, LCD_WIDTH);
  const uint16_t y0 = clampDisplayCoordinate(sy, LCD_HEIGHT);
  const uint16_t x1 = clampDisplayCoordinate(ex, LCD_WIDTH);
  const uint16_t y1 = clampDisplayCoordinate(ey, LCD_HEIGHT);

  if (x0 < x1 && y0 < y1) {
    GUI_FillRectColor(x0, y0, x1, y1, color);
  }
}

static void calculateSt7920Layout(void) {
  const lcd_pixel_type availableHeight = LCD_HEIGHT - LCD_EMULATOR_TOP_MARGIN - LCD_EMULATOR_BOTTOM_MARGIN;
  lcd_pixel_type scale = minLcdPixel((lcd_pixel_type)LCD_WIDTH / (lcd_pixel_type)ST7920_ASPECT_WIDTH,
                                     availableHeight / (lcd_pixel_type)ST7920_ASPECT_HEIGHT);
#if !defined(LCD_FULLSCREEN)
  scale = floorf(scale);
#endif

  st7920Width = scale * (lcd_pixel_type)ST7920_ASPECT_WIDTH;
  st7920Height = scale * (lcd_pixel_type)ST7920_ASPECT_HEIGHT;
  st7920PixelWidth = st7920Width / (lcd_pixel_type)ST7920_GXROWS;
  st7920PixelHeight = st7920Height / (lcd_pixel_type)ST7920_GYROWS;
  st7920StartX = (LCD_WIDTH - st7920Width) / 2;
  st7920StartY = LCD_EMULATOR_TOP_MARGIN + (availableHeight - st7920Height) / 2;
}

#if defined(LCD_SD_LOGO_FOLDER)
  #define ST7920_FRAMEBUFFER_WIDTH 128
  #define ST7920_FRAMEBUFFER_HEIGHT 64
  #define ST7920_FRAMEBUFFER_BYTEWIDTH (ST7920_FRAMEBUFFER_WIDTH / 8)
  #define STATUS_LOGO_OVERRIDE_MARKER_SIZE 8
  #define STATUS_LOGO_OVERRIDE_MARKER_TYPES 4
  #define STATUS_LOGO_OVERRIDE_MAX_MARKER_HITS 12
  #define STATUS_LOGO_OVERRIDE_MIN_WIDTH 16
  #define STATUS_LOGO_OVERRIDE_MIN_HEIGHT 16
  #define STATUS_LOGO_ANIM_PATH_MAX 64
  #define STATUS_LOGO_ANIM_BYTES_PER_PIXEL 2

enum StatusLogoMarkerType {
  STATUS_MARKER_TOP_LEFT,
  STATUS_MARKER_TOP_RIGHT,
  STATUS_MARKER_BOTTOM_LEFT,
  STATUS_MARKER_BOTTOM_RIGHT
};

typedef struct {
  uint8_t x;
  uint8_t y;
} STATUS_LOGO_MARKER_HIT;

typedef struct {
  uint8_t x;
  uint8_t y;
  uint8_t w;
  uint8_t h;
} STATUS_LOGO_RECT;

// U8G/ST7920 bitmap bytes are compared in the same bit order used by drawLogicalByte().
static const uint8_t pStatusLogoMarkers[STATUS_LOGO_OVERRIDE_MARKER_TYPES][STATUS_LOGO_OVERRIDE_MARKER_SIZE] = {
  {0xFF, 0x81, 0xBD, 0xA5, 0xB5, 0x85, 0xFD, 0x01},
  {0xFF, 0x81, 0xBD, 0xA5, 0xAD, 0xA1, 0xBF, 0x80},
  {0x01, 0xFD, 0x85, 0xB5, 0xA5, 0xBD, 0x81, 0xFF},
  {0x80, 0xBF, 0xA1, 0xAD, 0xA5, 0xBD, 0x81, 0xFF}
};

static uint8_t pSt7920FrameBuffer[ST7920_FRAMEBUFFER_HEIGHT][ST7920_FRAMEBUFFER_BYTEWIDTH];
static STATUS_LOGO_RECT statusLogoOverrideRect = {0, 0, 0, 0};
static bool bStatusLogoOverrideActive = false;
static bool bStatusLogoOverrideFrameVisible = false;
static bool bStatusLogoOverrideScanDirty = false;
static uint16_t ui16StatusLogoAnimationFrame = 0;
static uint32_t ui32StatusLogoAnimationNextMs = 0;
#endif

#if LCD_ENCODER_SUPPORT && (defined(SPI_RESTART_KNOB_PRESS_DURATION_SEC) || defined(LCD_IDLE_TIMEOUT_SEC))
  #define LCD_ENCODER_POLLING
#endif
#if defined(KNOB_RGB_COLOR) || defined(LCD_ENCODER_POLLING) || defined(LCD_SD_TEXT_FILE) || defined(LCD_SD_LOGO_FOLDER)
  #define LCD_TIMER_TICK
#endif

#if defined(LCD_TITLE) || defined(LCD_SD_TEXT_FILE)
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
      uint8_t col = 0;
      while (col < LCD_TEXT_CHAR_WIDTH) {
        while (col < LCD_TEXT_CHAR_WIDTH && (rowBits & (1 << col)) == 0) {
          ++col;
        }
        uint8_t runStart = col;
        while (col < LCD_TEXT_CHAR_WIDTH && (rowBits & (1 << col)) != 0) {
          ++col;
        }
        if (runStart < col) {
          fillRect(x + runStart, y + row, col - runStart, 1, LCD_COLOR_FOREGROUND);
        }
      }
    }
    x += LCD_TEXT_CHAR_STEP;
    ++text;
  }
}

static void drawTitleBarText(const char *text) {
  drawTextLine(text, centeredTextX(text), 0);
}
#endif

static void drawLogicalByte(uint8_t x, uint8_t y, uint8_t d) {
  uint8_t runStart = 0;
  uint16_t runColor = (d & 0x01) ? LCD_COLOR_FOREGROUND : LCD_COLOR_BACKGROUND;

  for (uint8_t bit = 1; bit < 8; ++bit) {
    uint16_t color = (d & (1 << bit)) ? LCD_COLOR_FOREGROUND : LCD_COLOR_BACKGROUND;
    if (color != runColor) {
      fillRect(st7920StartX + (x + runStart) * st7920PixelWidth,
               st7920StartY + y * st7920PixelHeight,
               (bit - runStart) * st7920PixelWidth,
               st7920PixelHeight,
               runColor);
      runStart = bit;
      runColor = color;
    }
  }

  fillRect(st7920StartX + (x + runStart) * st7920PixelWidth,
           st7920StartY + y * st7920PixelHeight,
           (8 - runStart) * st7920PixelWidth,
           st7920PixelHeight,
           runColor);
}

#if defined(LCD_SD_LOGO_FOLDER)
static void statusLogoOverrideReset(void) {
  memset(pSt7920FrameBuffer, 0, sizeof(pSt7920FrameBuffer));
  statusLogoOverrideRect.x = 0;
  statusLogoOverrideRect.y = 0;
  statusLogoOverrideRect.w = 0;
  statusLogoOverrideRect.h = 0;
  bStatusLogoOverrideActive = false;
  bStatusLogoOverrideFrameVisible = false;
  bStatusLogoOverrideScanDirty = false;
  ui16StatusLogoAnimationFrame = 0;
  ui32StatusLogoAnimationNextMs = 0;
}

static void fillLogicalRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color) {
  if (w == 0 || h == 0) {
    return;
  }

  fillRect(st7920StartX + x * st7920PixelWidth,
           st7920StartY + y * st7920PixelHeight,
           w * st7920PixelWidth,
           h * st7920PixelHeight,
           color);
}

static bool statusLogoOverrideMarkerAt(uint8_t markerType, uint8_t x, uint8_t y) {
  if (x + STATUS_LOGO_OVERRIDE_MARKER_SIZE > ST7920_FRAMEBUFFER_WIDTH ||
      y + STATUS_LOGO_OVERRIDE_MARKER_SIZE > ST7920_FRAMEBUFFER_HEIGHT) {
    return false;
  }

  const uint8_t byteColumn = x >> 3;
  const uint8_t bitOffset = x & 0x07;

  for (uint8_t row = 0; row < STATUS_LOGO_OVERRIDE_MARKER_SIZE; ++row) {
    uint16_t rowBits = pSt7920FrameBuffer[y + row][byteColumn];
    if (bitOffset > 0 && byteColumn + 1 < ST7920_FRAMEBUFFER_BYTEWIDTH) {
      rowBits |= (uint16_t)pSt7920FrameBuffer[y + row][byteColumn + 1] << 8;
    }
    if ((uint8_t)(rowBits >> bitOffset) != pStatusLogoMarkers[markerType][row]) {
      return false;
    }
  }
  return true;
}

static bool statusLogoOverrideRectValid(STATUS_LOGO_RECT rect) {
  return rect.w >= STATUS_LOGO_OVERRIDE_MIN_WIDTH &&
         rect.h >= STATUS_LOGO_OVERRIDE_MIN_HEIGHT &&
         rect.x + rect.w <= ST7920_FRAMEBUFFER_WIDTH &&
         rect.y + rect.h <= ST7920_FRAMEBUFFER_HEIGHT;
}

static bool statusLogoOverrideByteIntersectsRect(uint8_t x, uint8_t y, STATUS_LOGO_RECT rect) {
  return statusLogoOverrideRectValid(rect) &&
         y >= rect.y &&
         y < rect.y + rect.h &&
         x < rect.x + rect.w &&
         x + 8 > rect.x;
}

static void statusLogoOverrideStoreHit(STATUS_LOGO_MARKER_HIT hits[][STATUS_LOGO_OVERRIDE_MAX_MARKER_HITS],
                                       uint8_t counts[],
                                       uint8_t markerType,
                                       uint8_t x,
                                       uint8_t y) {
  if (counts[markerType] >= STATUS_LOGO_OVERRIDE_MAX_MARKER_HITS) {
    return;
  }

  hits[markerType][counts[markerType]].x = x;
  hits[markerType][counts[markerType]].y = y;
  ++counts[markerType];
}

static bool statusLogoOverrideBetterRect(uint8_t score,
                                         uint16_t area,
                                         uint8_t bestScore,
                                         uint16_t bestArea) {
  return score > bestScore || (score == bestScore && area > bestArea);
}

static void statusLogoOverrideConsiderRect(STATUS_LOGO_RECT rect,
                                           STATUS_LOGO_RECT *bestRect,
                                           uint8_t *bestScore,
                                           uint16_t *bestArea,
                                           bool *found) {
  if (!statusLogoOverrideRectValid(rect)) {
    return;
  }

  uint8_t score = 2;
  if (statusLogoOverrideMarkerAt(STATUS_MARKER_TOP_LEFT, rect.x, rect.y)) {
    ++score;
  }
  if (statusLogoOverrideMarkerAt(STATUS_MARKER_TOP_RIGHT, rect.x + rect.w - STATUS_LOGO_OVERRIDE_MARKER_SIZE, rect.y)) {
    ++score;
  }
  if (statusLogoOverrideMarkerAt(STATUS_MARKER_BOTTOM_LEFT, rect.x, rect.y + rect.h - STATUS_LOGO_OVERRIDE_MARKER_SIZE)) {
    ++score;
  }
  if (statusLogoOverrideMarkerAt(STATUS_MARKER_BOTTOM_RIGHT,
                                 rect.x + rect.w - STATUS_LOGO_OVERRIDE_MARKER_SIZE,
                                 rect.y + rect.h - STATUS_LOGO_OVERRIDE_MARKER_SIZE)) {
    ++score;
  }

  uint16_t area = (uint16_t)rect.w * rect.h;
  if (statusLogoOverrideBetterRect(score, area, *bestScore, *bestArea)) {
    *bestRect = rect;
    *bestScore = score;
    *bestArea = area;
    *found = true;
  }
}

static bool statusLogoOverrideFindRect(STATUS_LOGO_RECT *rect) {
  STATUS_LOGO_MARKER_HIT hits[STATUS_LOGO_OVERRIDE_MARKER_TYPES][STATUS_LOGO_OVERRIDE_MAX_MARKER_HITS];
  uint8_t counts[STATUS_LOGO_OVERRIDE_MARKER_TYPES] = {0, 0, 0, 0};

  for (uint8_t y = 0; y <= ST7920_FRAMEBUFFER_HEIGHT - STATUS_LOGO_OVERRIDE_MARKER_SIZE; ++y) {
    for (uint8_t x = 0; x <= ST7920_FRAMEBUFFER_WIDTH - STATUS_LOGO_OVERRIDE_MARKER_SIZE; ++x) {
      for (uint8_t marker = 0; marker < STATUS_LOGO_OVERRIDE_MARKER_TYPES; ++marker) {
        if (statusLogoOverrideMarkerAt(marker, x, y)) {
          statusLogoOverrideStoreHit(hits, counts, marker, x, y);
        }
      }
    }
  }

  STATUS_LOGO_RECT bestRect = {0, 0, 0, 0};
  uint8_t bestScore = 0;
  uint16_t bestArea = 0;
  bool found = false;

  for (uint8_t i = 0; i < counts[STATUS_MARKER_TOP_LEFT]; ++i) {
    for (uint8_t j = 0; j < counts[STATUS_MARKER_BOTTOM_RIGHT]; ++j) {
      STATUS_LOGO_MARKER_HIT tl = hits[STATUS_MARKER_TOP_LEFT][i];
      STATUS_LOGO_MARKER_HIT br = hits[STATUS_MARKER_BOTTOM_RIGHT][j];
      if (br.x > tl.x && br.y > tl.y) {
        STATUS_LOGO_RECT candidate = {
          tl.x,
          tl.y,
          (uint8_t)(br.x - tl.x + STATUS_LOGO_OVERRIDE_MARKER_SIZE),
          (uint8_t)(br.y - tl.y + STATUS_LOGO_OVERRIDE_MARKER_SIZE)
        };
        statusLogoOverrideConsiderRect(candidate, &bestRect, &bestScore, &bestArea, &found);
      }
    }
  }

  for (uint8_t i = 0; i < counts[STATUS_MARKER_TOP_RIGHT]; ++i) {
    for (uint8_t j = 0; j < counts[STATUS_MARKER_BOTTOM_LEFT]; ++j) {
      STATUS_LOGO_MARKER_HIT tr = hits[STATUS_MARKER_TOP_RIGHT][i];
      STATUS_LOGO_MARKER_HIT bl = hits[STATUS_MARKER_BOTTOM_LEFT][j];
      if (tr.x > bl.x && bl.y > tr.y) {
        STATUS_LOGO_RECT candidate = {
          bl.x,
          tr.y,
          (uint8_t)(tr.x - bl.x + STATUS_LOGO_OVERRIDE_MARKER_SIZE),
          (uint8_t)(bl.y - tr.y + STATUS_LOGO_OVERRIDE_MARKER_SIZE)
        };
        statusLogoOverrideConsiderRect(candidate, &bestRect, &bestScore, &bestArea, &found);
      }
    }
  }

  if (found) {
    *rect = bestRect;
  }
  return found;
}

static void statusLogoOverrideRedrawRect(STATUS_LOGO_RECT rect) {
  uint8_t startByte = rect.x >> 3;
  uint8_t endByte = (rect.x + rect.w + 7) >> 3;
  if (endByte > ST7920_FRAMEBUFFER_BYTEWIDTH) {
    endByte = ST7920_FRAMEBUFFER_BYTEWIDTH;
  }

  for (uint8_t y = rect.y; y < rect.y + rect.h && y < ST7920_FRAMEBUFFER_HEIGHT; ++y) {
    for (uint8_t byteColumn = startByte; byteColumn < endByte; ++byteColumn) {
      drawLogicalByte(byteColumn * 8, y, pSt7920FrameBuffer[y][byteColumn]);
    }
  }
}

static uint32_t statusLogoAnimationIntervalMs(void) {
  const uint32_t intervalMs = (uint32_t)(LCD_SD_LOGO_DELAY_MS);
  if (intervalMs == 0) {
    return 1;
  }
  return intervalMs;
}

static void statusLogoAnimationFramePath(uint16_t frame, char path[STATUS_LOGO_ANIM_PATH_MAX]) {
  uint8_t pos = 0;
  const char prefix[] = LCD_SD_LOGO_FOLDER "/";
  const char suffix[] = ".frb";

  for (uint8_t i = 0; prefix[i] != '\0' && pos < STATUS_LOGO_ANIM_PATH_MAX - 1; ++i) {
    path[pos++] = prefix[i];
  }

  char digits[5];
  uint8_t digitCount = 0;
  do {
    digits[digitCount++] = (char)('0' + frame % 10);
    frame /= 10;
  } while (frame > 0 && digitCount < sizeof(digits));

  while (digitCount > 0 && pos < STATUS_LOGO_ANIM_PATH_MAX - 1) {
    path[pos++] = digits[--digitCount];
  }

  for (uint8_t i = 0; suffix[i] != '\0' && pos < STATUS_LOGO_ANIM_PATH_MAX - 1; ++i) {
    path[pos++] = suffix[i];
  }

  path[pos] = '\0';
}

static uint16_t statusLogoAnimationReadColor(const uint8_t *pBytes) {
  return (uint16_t)pBytes[0] | ((uint16_t)pBytes[1] << 8);
}

static void statusLogoAnimationDrawRow(STATUS_LOGO_RECT rect, uint8_t row, const uint8_t *pRowBytes) {
  if (rect.w == 0) {
    return;
  }

  uint8_t runStart = 0;
  uint16_t runColor = statusLogoAnimationReadColor(pRowBytes);

  for (uint8_t col = 1; col < rect.w; ++col) {
    uint16_t color = statusLogoAnimationReadColor(&pRowBytes[col * STATUS_LOGO_ANIM_BYTES_PER_PIXEL]);
    if (color != runColor) {
      fillLogicalRect(rect.x + runStart, rect.y + row, col - runStart, 1, runColor);
      runStart = col;
      runColor = color;
    }
  }

  fillLogicalRect(rect.x + runStart, rect.y + row, rect.w - runStart, 1, runColor);
}

static bool statusLogoAnimationDrawFrame(STATUS_LOGO_RECT rect, uint16_t frame) {
  char path[STATUS_LOGO_ANIM_PATH_MAX];
  statusLogoAnimationFramePath(frame, path);

  FIL file;
  if (f_open(&file, path, FA_OPEN_EXISTING | FA_READ) != FR_OK) {
    return false;
  }

  uint8_t pRowBytes[ST7920_FRAMEBUFFER_WIDTH * STATUS_LOGO_ANIM_BYTES_PER_PIXEL];
  const uint16_t rowBytes = (uint16_t)rect.w * STATUS_LOGO_ANIM_BYTES_PER_PIXEL;
  bool bReadOk = true;

  for (uint8_t row = 0; row < rect.h; ++row) {
    UINT bytesRead = 0;
    if (f_read(&file, pRowBytes, rowBytes, &bytesRead) != FR_OK || bytesRead != rowBytes) {
      bReadOk = false;
      break;
    }

    statusLogoAnimationDrawRow(rect, row, pRowBytes);
  }

  f_close(&file);
  if (!bReadOk) {
    statusLogoOverrideRedrawRect(rect);
  }
  return bReadOk;
}

static bool statusLogoOverrideSameRect(STATUS_LOGO_RECT a, STATUS_LOGO_RECT b) {
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static void statusLogoAnimationHideFrame(void) {
  if (bStatusLogoOverrideFrameVisible) {
    statusLogoOverrideRedrawRect(statusLogoOverrideRect);
  }
  bStatusLogoOverrideFrameVisible = false;
}

static void statusLogoAnimationResetFrames(uint32_t currentMs) {
  ui16StatusLogoAnimationFrame = 0;
  ui32StatusLogoAnimationNextMs = currentMs;
}

static void statusLogoAnimationDrawNextFrame(bool bSdMounted, uint32_t currentMs, bool bForce) {
  if (!bSdMounted) {
    statusLogoAnimationHideFrame();
    statusLogoAnimationResetFrames(currentMs + statusLogoAnimationIntervalMs());
    return;
  }

  if (!bForce && ((int32_t)(currentMs - ui32StatusLogoAnimationNextMs)) < 0) {
    return;
  }

  uint16_t frame = ui16StatusLogoAnimationFrame;
  bool bDrawn = statusLogoAnimationDrawFrame(statusLogoOverrideRect, frame);
  if (!bDrawn && frame != 0) {
    frame = 0;
    bDrawn = statusLogoAnimationDrawFrame(statusLogoOverrideRect, frame);
  }

  if (bDrawn) {
    bStatusLogoOverrideFrameVisible = true;
    ui16StatusLogoAnimationFrame = frame + 1;
  } else {
    statusLogoAnimationHideFrame();
    ui16StatusLogoAnimationFrame = 0;
  }

  ui32StatusLogoAnimationNextMs = currentMs + statusLogoAnimationIntervalMs();
}

static bool statusLogoOverrideObserveByte(uint8_t x, uint8_t y, uint8_t d) {
  if (y >= ST7920_FRAMEBUFFER_HEIGHT || x >= ST7920_FRAMEBUFFER_WIDTH || (x & 0x07) != 0) {
    return true;
  }

  uint8_t byteColumn = x >> 3;
  if (byteColumn >= ST7920_FRAMEBUFFER_BYTEWIDTH) {
    return true;
  }

  if (pSt7920FrameBuffer[y][byteColumn] != d) {
    pSt7920FrameBuffer[y][byteColumn] = d;
    if (!bStatusLogoOverrideActive || statusLogoOverrideByteIntersectsRect(x, y, statusLogoOverrideRect)) {
      bStatusLogoOverrideScanDirty = true;
    }
    return true;
  }
  return false;
}

static bool statusLogoOverrideShouldDrawByte(uint8_t x, uint8_t y, uint8_t d) {
  bool bSuppressDraw = bStatusLogoOverrideActive &&
                       bStatusLogoOverrideFrameVisible &&
                       statusLogoOverrideByteIntersectsRect(x, y, statusLogoOverrideRect);
  bool bChanged = statusLogoOverrideObserveByte(x, y, d);
  return !bSuppressDraw && bChanged;
}

static void statusLogoOverrideFlush(bool bSdMounted, uint32_t currentMs) {
  bool bForceFrame = false;

  if (bStatusLogoOverrideScanDirty) {
    STATUS_LOGO_RECT rect;
    if (statusLogoOverrideFindRect(&rect)) {
      bool bRectChanged = !bStatusLogoOverrideActive ||
                          !statusLogoOverrideSameRect(statusLogoOverrideRect, rect);
      if (bRectChanged) {
        statusLogoAnimationHideFrame();
        statusLogoOverrideRect = rect;
        statusLogoAnimationResetFrames(currentMs);
        bForceFrame = true;
      }
      bStatusLogoOverrideActive = true;
    } else if (bStatusLogoOverrideActive) {
      statusLogoAnimationHideFrame();
      statusLogoOverrideRect.x = 0;
      statusLogoOverrideRect.y = 0;
      statusLogoOverrideRect.w = 0;
      statusLogoOverrideRect.h = 0;
      bStatusLogoOverrideActive = false;
      statusLogoAnimationResetFrames(currentMs);
    }

    bStatusLogoOverrideScanDirty = false;
  }

  if (bStatusLogoOverrideActive) {
    statusLogoAnimationDrawNextFrame(bSdMounted, currentMs, bForceFrame);
  }
}
#endif

#if defined(LCD_SD_TEXT_FILE)
static bool msElapsed(uint32_t currentMs, uint32_t targetMs) {
  return ((int32_t)(currentMs - targetMs)) >= 0;
}

static uint16_t st7920BottomY(void) {
  lcd_pixel_type bottom = st7920StartY + st7920Height;
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
    fillRect(0, y, LCD_WIDTH, h, LCD_COLOR_BACKGROUND);
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
  ui32SdTextNextMs = currentMs + LCD_SD_TEXT_DELAY_SEC * 1000;
}
#endif

static void clearDisplay() {
#if defined(LCD_SD_LOGO_FOLDER)
  statusLogoOverrideReset();
#endif

  // Clear ST7920 gui rect
  fillRect(st7920StartX,
           st7920StartY,
           st7920Width,
           st7920Height,
           LCD_COLOR_BACKGROUND);
}

static void drawByte(uint8_t x, uint8_t y, uint8_t d) {
#if defined(LCD_SD_LOGO_FOLDER)
  if (!statusLogoOverrideShouldDrawByte(x, y, d)) {
    return;
  }
#endif
  drawLogicalByte(x, y, d);
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
  calculateSt7920Layout();

#if defined(LCD_SD_TEXT_FILE)
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
#if defined(LCD_ENCODER_POLLING) || defined(LCD_SD_TEXT_FILE) || defined(LCD_SD_LOGO_FOLDER)
    const uint32_t ui32CurrentMs = Timer_GetTimerMs();
#endif

    // Check if SPI data is available
    if (SPI_SlaveGetData(&data)) {
      // Parse data
      st7920Emulator.parseSerialData(data);

      // Update SPI data received indicator
#if defined(SPI_DATA_RECEIVED_INDICATOR) && defined(LCD_TITLE)
      if (bSpiDataIndicator) {
        // Draw new pixel
        fillRect(ui16DX, ui16DY, 1, 1, ui16DColor);

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
    } else {
#if defined(LCD_SD_LOGO_FOLDER)
      statusLogoOverrideFlush(bSdMounted, ui32CurrentMs);
#endif
    }

    // Update SPI activation display
#if defined(SPI_DATA_RECEIVED_INDICATOR) && defined(LCD_TITLE)
    if (bSpiActivityIndicator) {
      while (ui32LastSpiActivated < ui32SpiActivated) {
        // Draw new pixel
        fillRect(ui16AX, ui16AY, 10, 1, ui16AColor);

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
      uint32_t ui32BtnPressDuration = ui32CurrentMs - ui32FirstBtnPress;

      if (ui32BtnPressDuration >= SPI_RESTART_KNOB_PRESS_DURATION_SEC * 1000) {
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
      uint32_t ui32IdleDuration = ui32CurrentMs - ui32LastActive;

      // Check inactivity time
      if (ui32IdleDuration >= LCD_IDLE_TIMEOUT_SEC * 1000) {
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

#if defined(LCD_SD_TEXT_FILE)
    updateSdTextOverlay(bSdMounted, ui32CurrentMs);
#endif
  }
}
