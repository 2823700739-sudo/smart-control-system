#ifndef __MYLCD_H_
#define __MYLCD_H_

#include <stdint.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_RST(x) x ? gpio_set_level(GPIO_NUM_21,1) : gpio_set_level(GPIO_NUM_21,0)
#define LCD_DC(x)  x ? gpio_set_level(GPIO_NUM_38,1) : gpio_set_level(GPIO_NUM_38,0)
#define LCD_CS(x)  // 本例不需要CS
#define LCD_BLK(x) x ? gpio_set_level(GPIO_NUM_39,1) : gpio_set_level(GPIO_NUM_39,0)

#define WHITE           0xFFFF      /* 白色 */
#define BLACK           0x0000      /* 黑色 */
#define RED             0xF800      /* 红色 */
#define GREEN           0x07E0      /* 绿色 */
#define BLUE            0x001F      /* 蓝色 */ 
#define MAGENTA         0XF81F      /* 品红色/紫红色 = BLUE + RED */
#define YELLOW          0XFFE0      /* 黄色 = GREEN + RED */
#define CYAN            0X07FF      /* 青色 = GREEN + BLUE */  
#define BROWN           0XBC40      /* 棕色 */
#define BRRED           0XFC07      /* 棕红色 */
#define GRAY            0X8430      /* 灰色 */ 
#define DARKBLUE        0X01CF      /* 深蓝色 */
#define LIGHTBLUE       0X7D7C      /* 浅蓝色 */ 
#define GRAYBLUE        0X5458      /* 灰蓝色 */ 
#define LIGHTGREEN      0X841F      /* 浅绿色 */  
#define LGRAY           0XC618      /* 浅灰色(PANNEL),窗体背景色 */ 
#define LGRAYBLUE       0XA651      /* 浅灰蓝色(中间层颜色) */ 
#define LBBLUE          0X2B12      /* 浅棕蓝色(选择条目的反色) */ 

extern uint8_t *lcd_buf;

void lcd_init(void);
void lcd_set_window(uint16_t xstar, uint16_t ystar, uint16_t xend, uint16_t yend);
void lcd_show_dot(uint16_t color);
void lcd_show_line(uint16_t color);
void lcd_clear(uint16_t color);
void lcd_show_picture(uint8_t *img);
void lcd_write_datan(uint8_t *data, uint16_t length);
void lcd_write_data16(uint16_t data);

#ifdef __cplusplus
}
#endif

#endif