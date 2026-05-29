#include "led.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "LED";

#define LED_GPIO         GPIO_NUM_46
#define LEDC_TIMER       LEDC_TIMER_1
#define LEDC_CHANNEL     LEDC_CHANNEL_1
#define LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LEDC_RESOLUTION  LEDC_TIMER_10_BIT
#define LEDC_FREQ        5000

static const uint32_t duty_map[4] = {1023, 767, 409, 0};

void led_init(void)
{
    gpio_config_t gpio_structure = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_INPUT_OUTPUT,
        .pin_bit_mask = (1ull << LED_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&gpio_structure));
}

void led_pwm_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = LEDC_RESOLUTION,
        .timer_num       = LEDC_TIMER,
        .freq_hz         = LEDC_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {
        .gpio_num   = LED_GPIO,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    ESP_LOGI(TAG, "PWM ready on GPIO %d", LED_GPIO);
}

void led_set_brightness(int level)
{
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty_map[level]));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
}

void gpio_toggle(gpio_num_t gpio_num)
{
    gpio_set_level(gpio_num, gpio_get_level(gpio_num) ? 0 : 1);
}
