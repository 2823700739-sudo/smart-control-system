#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "apds9960.h"

static const char *TAG = "APDS9960";

// I2C 配置
#define I2C_MASTER_SCL_IO           GPIO_NUM_2      // 你的 SCL
#define I2C_MASTER_SDA_IO           GPIO_NUM_1      // 你的 SDA
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          100000          // 标准 100kHz

// APDS9960 中断引脚
#define APDS9960_INT_IO             GPIO_NUM_3

// 手势事件处理（回调）
static void gesture_callback(uint8_t gesture)
{
    switch (gesture) {
    case APDS9960_UP:
        printf("向上\n");
        break;
    case APDS9960_DOWN:
        printf("向下\n");
        break;
    case APDS9960_LEFT:
        printf("向左\n");
        break;
    case APDS9960_RIGHT:
        printf("向右\n");
        break;
    default:
        // printf("未知手势\n");
        break;
    }
}

void app_main(void)
{
    // 1. 初始化 I2C 主机总线（使用 ESP-IDF v5 现代 API 避免 "未初始化" 的错误打印）
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_MASTER_NUM,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));

    // 这时候再调用 i2c_bus_create 就能找到已经初始化的总线，不会再产生 ERROR 打印
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_bus_handle_t i2c_bus = i2c_bus_create(I2C_MASTER_NUM, &conf);
    if (!i2c_bus) {
        ESP_LOGE(TAG, "I2C bus creation failed!");
        return;
    }

    // 2. 初始化 APDS-9960
    apds9960_handle_t apds9960 = apds9960_create(i2c_bus, APDS9960_I2C_ADDRESS);
    if (!apds9960) {
        ESP_LOGE(TAG, "APDS-9960 device creation failed!");
        return;
    }
    
    uint8_t dev_id = 0;
    apds9960_get_deviceid(apds9960, &dev_id);
    ESP_LOGI(TAG, "APDS-9960 Device ID: 0x%02X", dev_id);
    if (dev_id != APDS9960_WHO_AM_I_VAL) {
        ESP_LOGE(TAG, "Invalid Device ID! Expected 0x%02X, got 0x%02X. Check wiring/power.", APDS9960_WHO_AM_I_VAL, dev_id);
    }

    esp_err_t err = apds9960_gesture_init(apds9960);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "APDS-9960 gesture init failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "APDS-9960 gesture test started...");

    while (1) {
        if (apds9960_gesture_valid(apds9960)) {
            uint8_t gesture = apds9960_read_gesture(apds9960);
            gesture_callback(gesture);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}