#include "led.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char * TAG = "LED";

void led_init(void)
{
	esp_err_t err = ESP_FAIL;

    gpio_config_t gpio_structure = {
		.intr_type = GPIO_INTR_DISABLE,//GPIO中断模式
		.mode = GPIO_MODE_INPUT_OUTPUT,//GPIO模式:输入输出
		.pin_bit_mask = (1ull << GPIO_NUM_46),//GPIO位掩码，指定要配置的GPIO引脚（包含你的引脚9和46）
		.pull_down_en = GPIO_PULLDOWN_DISABLE,//GPIO下拉使能
		.pull_up_en = GPIO_PULLUP_ENABLE,//GPIO上拉使能
	};
    err = gpio_config(&gpio_structure);
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG,"gpio_config() error!\r\n");
	}
}

void gpio_toggle(gpio_num_t gpio_num)//切换GPIO引脚的电平状态
{
	if( gpio_get_level( gpio_num ) == 1 )
	{
		gpio_set_level(gpio_num, 0);
	}
	else
	{
		gpio_set_level(gpio_num, 1);
	}
}