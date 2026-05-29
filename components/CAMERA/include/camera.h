#ifndef __CAMERA_H__
#define __CAMERA_H__

#include <stdint.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAM_PIN_PWDN     GPIO_NUM_16
#define CAM_PIN_RESET    GPIO_NUM_15
#define CAM_PIN_XCLK     GPIO_NUM_NC
#define CAM_PIN_SIOD     GPIO_NUM_19
#define CAM_PIN_SIOC     GPIO_NUM_18
#define CAM_PIN_D7       GPIO_NUM_11
#define CAM_PIN_D6       GPIO_NUM_10
#define CAM_PIN_D5       GPIO_NUM_9
#define CAM_PIN_D4       GPIO_NUM_8
#define CAM_PIN_D3       GPIO_NUM_7
#define CAM_PIN_D2       GPIO_NUM_6
#define CAM_PIN_D1       GPIO_NUM_5
#define CAM_PIN_D0       GPIO_NUM_4
#define CAM_PIN_VSYNC    GPIO_NUM_14
#define CAM_PIN_HREF     GPIO_NUM_13
#define CAM_PIN_PCLK     GPIO_NUM_12

void camera_init(void);
void camera_show(uint16_t x, uint16_t y);

#ifdef __cplusplus
}
#endif

#endif
