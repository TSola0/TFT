/**
 *******************************************************************************
 * @file    lcd.h
 * @brief   2.8 寸 SPI TFT 液晶驱动（ILI9341 控制器，320×240）
 *
 * 适配硬件：
 *   - 主控    : STM32F103C8T6（72MHz，SPI1 主机模式）
 *   - 液晶    : 2.8 寸 8 引脚 SPI 模块（VCC/GND/SDI/SCL/CS/SDO/D-C/BLK）
 *   - 控制器  : ILI9341（本模块无硬件 RST 引脚，采用软件复位）
 *
 * 使用说明：
 *   1. 本驱动依赖 CubeMX 生成的 spi.c 中的 hspi1 句柄；
 *   2. 引脚通过下方宏定义统一管理，改引脚只需改这一处；
 *   3. 调用 LCD_Init() 完成初始化，之后即可使用各绘图接口；
 *   4. 所有绘图函数坐标原点默认在屏幕左上角（横屏 320×240）。
 *
 * @author  梦咕咕 & Shiro
 * @version V1.0
 * @date    2026-09-08
 *******************************************************************************
 */
#ifndef __LCD_H__
#define __LCD_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* ============================ 引脚配置区（按需修改） ======================== */

/** SPI 句柄：驱动使用的 SPI 外设（CubeMX 生成于 spi.c） */
#define LCD_SPI_HANDLE              hspi1

/** 片选脚 CS  -> 接屏幕 CS */
#define LCD_CS_PORT                 GPIOA
#define LCD_CS_PIN                  GPIO_PIN_4

/** 数据/命令选择脚 D/C -> 接屏幕 D/C */
#define LCD_DC_PORT                 GPIOA
#define LCD_DC_PIN                  GPIO_PIN_8

/** 背光控制脚 BL -> 接屏幕 BLK（本模块无 RST 脚，复位走软件命令 0x01） */
#define LCD_BL_PORT                 GPIOB
#define LCD_BL_PIN                  GPIO_PIN_1

/* ============================ 底层操作宏（勿随意改动） ====================== */

/** 拉低/拉高片选：选中 / 释放液晶 */
#define LCD_CS_LOW()                HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_RESET)
#define LCD_CS_HIGH()               HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_SET)

/** D/C = 0 表示写命令，D/C = 1 表示写显示数据 */
#define LCD_DC_CMD()                HAL_GPIO_WritePin(LCD_DC_PORT,  LCD_DC_PIN,  GPIO_PIN_RESET)
#define LCD_DC_DATA()               HAL_GPIO_WritePin(LCD_DC_PORT,  LCD_DC_PIN,  GPIO_PIN_SET)

/** 背光开 / 关 */
#define LCD_BL_ON()                 HAL_GPIO_WritePin(LCD_BL_PORT,  LCD_BL_PIN,  GPIO_PIN_SET)
#define LCD_BL_OFF()                HAL_GPIO_WritePin(LCD_BL_PORT,  LCD_BL_PIN,  GPIO_PIN_RESET)

/* ============================ 屏幕参数配置 ================================= */

/** 屏幕方向：0=竖屏(240x320) 1=横屏(320x240) 2=竖屏翻转 3=横屏翻转 */
#define LCD_ROTATION_DEFAULT        1

/** 像素颜色格式：RGB565（高字节在前发送） */
#define LCD_COLOR(r, g, b)          ((uint16_t)((((r) & 0xF8) << 8) | \
                                                (((g) & 0xFC) << 3) | \
                                                (((b) & 0xF8) >> 3)))

/** 常用颜色（RGB565） */
#define LCD_COLOR_BLACK             0x0000
#define LCD_COLOR_NAVY              0x000F
#define LCD_COLOR_BLUE              0x001F
#define LCD_COLOR_GREEN             0x07E0
#define LCD_COLOR_CYAN              0x07FF
#define LCD_COLOR_RED               0xF800
#define LCD_COLOR_MAGENTA           0xF81F
#define LCD_COLOR_YELLOW            0xFFE0
#define LCD_COLOR_WHITE             0xFFFF
#define LCD_COLOR_GRAY              0x8410
#define LCD_COLOR_ORANGE            0xFD20
#define LCD_COLOR_PINK              0xF81F

/* ============================ 函数声明 ===================================== */

void    LCD_Init(void);
void    LCD_SetRotation(uint8_t rotation);
void    LCD_SetBacklight(uint8_t enable);
uint8_t LCD_GetRotation(void);
uint16_t LCD_GetWidth(void);
uint16_t LCD_GetHeight(void);

void    LCD_SetWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void    LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color);
void    LCD_Fill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void    LCD_Clear(uint16_t color);
void    LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void    LCD_DrawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *bmp);

void    LCD_ShowChar(uint16_t x, uint16_t y, uint8_t ch, uint16_t fg, uint16_t bg);
void    LCD_ShowString(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg);

#ifdef __cplusplus
}
#endif

#endif /* __LCD_H__ */
