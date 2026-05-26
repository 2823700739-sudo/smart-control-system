#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "apds9960.h"
#include "led.h"

static const char *TAG = "MAIN";

// ========== APDS9960 配置（使用 I2C_NUM_1，引脚 GPIO1/2）==========
#define APDS9960_I2C_PORT        I2C_NUM_1
#define APDS9960_SCL_IO          GPIO_NUM_2
#define APDS9960_SDA_IO          GPIO_NUM_1
#define APDS9960_I2C_ADDR        0x39
#define I2C_MASTER_FREQ_HZ       100000

// ========== OV2640 摄像头配置（强制使用 I2C_NUM_0，引脚 GPIO18/19）==========
#define CAM_PWDN_GPIO            GPIO_NUM_16
#define CAM_RESET_GPIO           GPIO_NUM_15
#define CAM_XCLK_GPIO            -1
#define CAM_SIOD_GPIO            GPIO_NUM_19
#define CAM_SIOC_GPIO            GPIO_NUM_18

#define CAM_D7_GPIO              GPIO_NUM_11
#define CAM_D6_GPIO              GPIO_NUM_10
#define CAM_D5_GPIO              GPIO_NUM_9
#define CAM_D4_GPIO              GPIO_NUM_8
#define CAM_D3_GPIO              GPIO_NUM_7
#define CAM_D2_GPIO              GPIO_NUM_6
#define CAM_D1_GPIO              GPIO_NUM_5
#define CAM_D0_GPIO              GPIO_NUM_4

#define CAM_VSYNC_GPIO           GPIO_NUM_14
#define CAM_HREF_GPIO            GPIO_NUM_13
#define CAM_PCLK_GPIO            GPIO_NUM_12

// 手势回调
static void gesture_callback(uint8_t gesture)
{
    switch (gesture) {
        case APDS9960_UP:    ESP_LOGI(TAG, "手势：向上"); break;
        case APDS9960_DOWN:  ESP_LOGI(TAG, "手势：向下"); break;
        case APDS9960_LEFT:  ESP_LOGI(TAG, "手势：向左"); gpio_toggle(GPIO_NUM_46); break;
        case APDS9960_RIGHT: ESP_LOGI(TAG, "手势：向右"); gpio_toggle(GPIO_NUM_46); break;
        default: break;
    }
}

// 初始化 APDS9960（独立 I2C_NUM_1）
static apds9960_handle_t init_apds9960(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = APDS9960_SDA_IO,
        .scl_io_num = APDS9960_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_bus_handle_t i2c_bus = i2c_bus_create(APDS9960_I2C_PORT, &conf);
    if (!i2c_bus) {
        ESP_LOGE(TAG, "APDS9960 I2C bus creation failed");
        return NULL;
    }

    apds9960_handle_t apds9960 = apds9960_create(i2c_bus, APDS9960_I2C_ADDR);
    if (!apds9960) {
        ESP_LOGE(TAG, "APDS9960 device creation failed");
        return NULL;
    }

    uint8_t dev_id = 0;
    apds9960_get_deviceid(apds9960, &dev_id);
    ESP_LOGI(TAG, "APDS9960 Device ID: 0x%02X", dev_id);
    if (dev_id != APDS9960_WHO_AM_I_VAL) {
        ESP_LOGE(TAG, "Invalid APDS9960 Device ID! Check wiring.");
        return NULL;
    }

    esp_err_t err = apds9960_gesture_init(apds9960);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "APDS9960 gesture init failed: %s", esp_err_to_name(err));
        return NULL;
    }

    ESP_LOGI(TAG, "APDS9960 gesture sensor ready");
    return apds9960;
}

// 初始化 OV2640 摄像头（强制使用 I2C_NUM_0）
static esp_err_t init_camera(void)
{
    // 手动复位和使能引脚
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CAM_PWDN_GPIO) | (1ULL << CAM_RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    gpio_set_level(CAM_PWDN_GPIO, 0);   // 使能摄像头
    gpio_set_level(CAM_RESET_GPIO, 1);  // 解除复位
    vTaskDelay(pdMS_TO_TICKS(10));

    camera_config_t config = {
        .pin_pwdn  = CAM_PWDN_GPIO,
        .pin_reset = CAM_RESET_GPIO,
        .pin_xclk  = CAM_XCLK_GPIO,
        .pin_sscb_sda = CAM_SIOD_GPIO,
        .pin_sscb_scl = CAM_SIOC_GPIO,

        .pin_d7 = CAM_D7_GPIO,
        .pin_d6 = CAM_D6_GPIO,
        .pin_d5 = CAM_D5_GPIO,
        .pin_d4 = CAM_D4_GPIO,
        .pin_d3 = CAM_D3_GPIO,
        .pin_d2 = CAM_D2_GPIO,
        .pin_d1 = CAM_D1_GPIO,
        .pin_d0 = CAM_D0_GPIO,

        .pin_vsync = CAM_VSYNC_GPIO,
        .pin_href  = CAM_HREF_GPIO,
        .pin_pclk  = CAM_PCLK_GPIO,

        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 12,
        .fb_count = 1,

        // .sccb_i2c_port = 0,           // 强制使用 I2C_NUM_0，避免与 APDS9960 冲突
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
        return err;
    }
    ESP_LOGI(TAG, "Camera initialized");
    return ESP_OK;
}

void app_main(void)
{
    led_init();  // 初始化 GPIO46 LED

    // 1. 初始化手势传感器（使用 I2C_NUM_1）
    apds9960_handle_t apds9960 = init_apds9960();
    if (!apds9960) {
        ESP_LOGW(TAG, "APDS9960 not available, gesture disabled");
    }

    // 2. 初始化摄像头（使用 I2C_NUM_0）
    if (init_camera() != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed, system halt");
        return;
    }

    // 3. 主循环：手势轮询 50ms，拍照间隔 5 秒
    uint32_t last_capture_time = 0;
    const uint32_t capture_interval_ms = 5000;
    uint32_t current_time;

    while (1) {
        current_time = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        // 手势轮询
        if (apds9960 && apds9960_gesture_valid(apds9960)) {
            uint8_t gesture = apds9960_read_gesture(apds9960);
            gesture_callback(gesture);
        }

        // 定时拍照
        if ((current_time - last_capture_time) >= capture_interval_ms) {
            camera_fb_t *fb = esp_camera_fb_get();
            if (fb) {
                ESP_LOGI(TAG, "Captured %zu bytes", fb->len);
                esp_camera_fb_return(fb);
            } else {
                ESP_LOGE(TAG, "Capture failed");
            }
            last_capture_time = current_time;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}