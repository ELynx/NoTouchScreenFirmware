#include "GUI.h"
#include "includes.h"

void LCD_SetWindow(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey)
{
  LCD_WR_REG(0x2A);
  LCD_WR_DATA(sx>>8);LCD_WR_DATA(sx&0xFF);
  LCD_WR_DATA(ex>>8);LCD_WR_DATA(ex&0xFF);
  LCD_WR_REG(0x2B);
  LCD_WR_DATA(sy>>8);LCD_WR_DATA(sy&0xFF);
  LCD_WR_DATA(ey>>8);LCD_WR_DATA(ey&0xFF);
}

void GUI_Clear(uint16_t color)
{
  GUI_FillRectColor(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

void GUI_FillRectColor(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color)
{
  if (sx >= ex || sy >= ey) return;

  LCD_SetWindow(sx, sy, ex-1, ey-1);
  LCD_WR_REG(0x2C);

  uint32_t index;
  const uint32_t area = (uint32_t)(ex - sx) * (uint32_t)(ey - sy);
  for(index=0; index<area; ++index)
  {
    LCD_WR_16BITS_DATA(color);
  }
}
