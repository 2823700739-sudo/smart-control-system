/**
 * @file camera.c
 * @brief ESP32-S3 摄像头驱动封装
 * 
 * 封装了 ESP32-CAM 摄像头模块的初始化和图像显示功能
 */

#include "camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "lcd.h"

// 外部引用 LCD 缓冲区
extern uint8_t *lcd_buf;

/**
 * @brief 初始化摄像头模块
 * 
 * 配置摄像头电源和复位引脚，初始化摄像头硬件
 */
void camera_init(void)
{
    // 配置电源和复位引脚为输出模式
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CAM_PIN_PWDN) | (1ULL << CAM_PIN_RESET),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);

    // 摄像头电源控制序列
    gpio_set_level(CAM_PIN_PWDN, 1);   // 打开电源
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(CAM_PIN_PWDN, 0);   // 关闭电源（低电平有效）
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(CAM_PIN_RESET, 0);  // 复位摄像头
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(CAM_PIN_RESET, 1);  // 释放复位
    vTaskDelay(pdMS_TO_TICKS(20));

    // 摄像头配置结构体
    camera_config_t camera_config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,
        .xclk_freq_hz = 20000000,      // XCLK 时钟频率 20MHz
        .ledc_timer = LEDC_TIMER_0,     // LEDC 定时器
        .ledc_channel = LEDC_CHANNEL_0, // LEDC 通道
        .pixel_format = PIXFORMAT_RGB565, // 像素格式 RGB565
        .frame_size = FRAMESIZE_240X240, // 帧大小 240x240
        .jpeg_quality = 12,             // JPEG 质量
        .fb_count = 2,                  // 帧缓冲区数量
        .fb_location = CAMERA_FB_IN_PSRAM, // 帧缓冲区位置（PSRAM）
    };

    // 初始化摄像头
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE("CAMERA", "摄像头初始化失败: 0x%x", err);
    } else {
        ESP_LOGI("CAMERA", "摄像头初始化成功");
    }
}

/**
 * @brief 在 LCD 上显示摄像头图像
 * @param x 显示起始 X 坐标
 * @param y 显示起始 Y 坐标
 */
void camera_show(uint16_t x, uint16_t y)
{
    // 获取摄像头帧缓冲区
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE("CAMERA", "获取帧缓冲区失败");
        return;
    }

    // 设置 LCD 显示窗口
    lcd_set_window(x, y, x + fb->width - 1, y + fb->height - 1);

    // 将帧数据复制到 LCD 缓冲区
    for (unsigned long j = 0; j < fb->width * fb->height; j++) {
        lcd_buf[2 * j] = fb->buf[2 * j];     // 高字节
        lcd_buf[2 * j + 1] = fb->buf[2 * j + 1]; // 低字节
    }

    // 分块写入 LCD（每块 11520 字节）
    unsigned long total_bytes = fb->width * fb->height * 2;
    for (unsigned long j = 0; j < (total_bytes / 11520); j++) {
        lcd_write_datan(&lcd_buf[j * 11520], 11520);
    }

    // 返回帧缓冲区
    esp_camera_fb_return(fb);
}
