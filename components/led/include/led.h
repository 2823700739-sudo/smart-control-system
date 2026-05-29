#ifndef __LED_H_
#define __LED_H_

#include "driver/gpio.h"

void led_init(void);
void led_pwm_init(void);
void led_set_brightness(int level);
void gpio_toggle(gpio_num_t gpio_num);

#endif
