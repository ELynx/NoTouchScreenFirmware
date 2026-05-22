#ifndef _GUI_H_
#define _GUI_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// fills whole display with color
void GUI_Clear(uint16_t color);

// fills area from start x, y to end x, y with color
void GUI_FillRectColor(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color);

// set currently active area from start x, y to end x, y
bool GUI_SetWindow(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey);

// fills next pixel in active area with color
void GUI_NextColor(uint16_t color);

#ifdef __cplusplus
}
#endif

#endif
