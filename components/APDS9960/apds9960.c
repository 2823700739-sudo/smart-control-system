/**
 * @file apds9960.c
 * @brief APDS9960 手势传感器驱动封装
 * 
 * 封装了 APDS9960 手势识别传感器的初始化和手势检测功能
 * 支持上下左右四个方向的手势识别
 * 
 * 实际接线：
 * - VL   -> 3.3V
 * - GND  -> GND
 * - VCC  -> 3.3V
 * - SDA  -> IO1
 * - SCL  -> IO2
 * - INT  -> IO3
 */

#include <stdio.h>
#include "apds9960.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

// 日志标签
static const char *TAG = "APDS9960";

// 包含 APDS9960 驱动头文件（优先使用 managed component）
#if __has_include("../../managed_components/espressif__apds9960/include/apds9960.h")
#include "../../managed_components/espressif__apds9960/include/apds9960.h"
#else
#include "apds9960_managed_fallback.h"
#endif

// I2C 配置 - 根据实际接线
#define APDS9960_I2C_PORT    I2C_NUM_0        // I2C 端口
#define APDS9960_SCL_IO      GPIO_NUM_2       // SCL 引脚 (IO2)
#define APDS9960_SDA_IO      GPIO_NUM_1       // SDA 引脚 (IO1)
#define APDS9960_INT_IO      GPIO_NUM_3       // INT 引脚 (IO3)
#define APDS9960_I2C_ADDR    APDS9960_I2C_ADDRESS  // 设备地址
#define I2C_MASTER_FREQ_HZ   400000           // I2C 频率

// 初始化配置
#define POWER_ON_DELAY_MS    100              // 上电稳定延时(ms)
#define RETRY_COUNT          3                // 设备检测重试次数
#define RETRY_DELAY_MS       200              // 重试间隔(ms)
#define POST_INIT_DELAY_MS   500              // 初始化后延迟启动轮询(ms)

// 手势控制全局变量（供主程序访问）
volatile int g_gesture_override   = 0;        // 手势覆盖标志
volatile int g_gesture_brightness = 0;        // 手势对应的亮度值

// APDS9960 设备句柄
static apds9960_handle_t s_apds9960 = NULL;
static i2c_bus_handle_t s_i2c_bus = NULL;

/**
 * @brief 手势轮询任务
 * 
 * 持续轮询 APDS9960 手势传感器，检测手势并更新全局变量
 * @param pv 参数（未使用）
 */
static void gesture_poll_task(void *pv)
{
    int64_t last_gesture = 0;  // 上次手势检测时间
    bool first_run = true;      // 首次运行标志

    while (1) {
        // 首次运行时输出就绪信息
        if (first_run) {
            ESP_LOGI(TAG, "手势识别已就绪，可以开始识别手势");
            first_run = false;
        }

        // 读取手势状态
        uint8_t gesture = apds9960_read_gesture(s_apds9960);

        // 手势防抖：1.5秒内只响应一次手势
        if (gesture != 0) {
            int64_t now = esp_timer_get_time();
            if (now - last_gesture < 1500000) {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            last_gesture = now;
        }

        // 处理手势
        switch (gesture) {
            case APDS9960_UP:
                ESP_LOGI(TAG, "手势检测: 上");
                break;
            case APDS9960_DOWN:
                ESP_LOGI(TAG, "手势检测: 下");
                break;
            case APDS9960_LEFT:
                ESP_LOGI(TAG, "手势检测: 左 -> LED 开(高亮度)");
                g_gesture_override   = 1;
                g_gesture_brightness = 3;  // 高亮度
                break;
            case APDS9960_RIGHT:
                ESP_LOGI(TAG, "手势检测: 右 -> LED 关"); 
                g_gesture_override   = 1;
                g_gesture_brightness = 0;   // 关闭
                break;
            default:
                break;
        }

        // 50ms 延时后继续轮询
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/**
 * @brief 初始化 INT 引脚
 */
static void apds9960_int_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = BIT(APDS9960_INT_IO),
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&cfg);
    ESP_LOGI(TAG, "INT 引脚 (GPIO%d) 配置为输入模式", APDS9960_INT_IO);
}

/**
 * @brief 初始化 APDS9960 手势传感器
 * @return APDS9960 设备句柄，失败返回 NULL
 */
apds9960_handle_t init_apds9960(void)
{
    ESP_LOGI(TAG, "开始初始化手势传感器...");

    // 步骤1: 等待传感器上电稳定
    ESP_LOGI(TAG, "等待传感器上电稳定 (%dms)...", POWER_ON_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(POWER_ON_DELAY_MS));

    // 步骤2: 配置 INT 引脚
    ESP_LOGI(TAG, "配置 INT 引脚...");
    apds9960_int_init();

    // 步骤3: 配置 I2C 总线
    ESP_LOGI(TAG, "配置 I2C 总线 (SDA:GPIO%d, SCL:GPIO%d)...", 
             APDS9960_SDA_IO, APDS9960_SCL_IO);
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = APDS9960_SDA_IO,
        .scl_io_num = APDS9960_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    
    // 创建 I2C 总线
    s_i2c_bus = i2c_bus_create(APDS9960_I2C_PORT, &conf);
    if (!s_i2c_bus) {
        ESP_LOGE(TAG, "I2C 总线创建失败");
        return NULL;
    }
    ESP_LOGI(TAG, "I2C 总线创建成功");

    // 步骤4: 创建设备并验证（带重试机制）
    apds9960_handle_t apds9960 = NULL;
    uint8_t dev_id = 0;
    int retry = 0;
    
    for (retry = 0; retry < RETRY_COUNT; retry++) {
        // 创建 APDS9960 设备
        apds9960 = apds9960_create(s_i2c_bus, APDS9960_I2C_ADDR);
        if (!apds9960) {
            ESP_LOGW(TAG, "第 %d 次尝试: 设备创建失败，%dms 后重试", retry + 1, RETRY_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
            continue;                                
        }

        // 读取设备 ID
        esp_err_t err = apds9960_get_deviceid(apds9960, &dev_id);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "第 %d 次尝试: 读取设备 ID 失败，%dms 后重试", retry + 1, RETRY_DELAY_MS);
            apds9960_delete(&apds9960);
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
            continue;
        }

        // 验证设备 ID
        if (dev_id != APDS9960_WHO_AM_I_VAL) {
            ESP_LOGW(TAG, "第 %d 次尝试: 设备 ID 不匹配 (0x%02X != 0x%02X)，%dms 后重试", 
                     retry + 1, dev_id, APDS9960_WHO_AM_I_VAL, RETRY_DELAY_MS);
            apds9960_delete(&apds9960);
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
            continue;
        }

        // 设备验证成功
        ESP_LOGI(TAG, "设备 ID 验证成功: 0x%02X", dev_id);
        break;
    }

    // 检查是否成功
    if (!apds9960 || dev_id != APDS9960_WHO_AM_I_VAL) {
        ESP_LOGE(TAG, "设备验证失败，已重试 %d 次", RETRY_COUNT);
        if (s_i2c_bus) {
            i2c_bus_delete(&s_i2c_bus);
            s_i2c_bus = NULL;
        }
        return NULL;
    }

    // 步骤5: 初始化手势识别功能
    ESP_LOGI(TAG, "初始化手势识别功能...");
    esp_err_t err = apds9960_gesture_init(apds9960);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "手势初始化失败: %s", esp_err_to_name(err));
        apds9960_delete(&apds9960);
        i2c_bus_delete(&s_i2c_bus);
        s_i2c_bus = NULL;
        return NULL;
    }
    ESP_LOGI(TAG, "手势识别初始化成功");

    // 步骤6: 保存设备句柄
    s_apds9960 = apds9960;

    // 步骤7: 延迟后创建轮询任务，确保传感器完全就绪
    ESP_LOGI(TAG, "等待 %dms 让传感器稳定...", POST_INIT_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(POST_INIT_DELAY_MS));

    // 步骤8: 创建手势轮询任务
    ESP_LOGI(TAG, "创建手势轮询任务...");
    BaseType_t ret = xTaskCreate(gesture_poll_task, "apds9960_poll", 4096, NULL, 3, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建轮询任务失败");
        apds9960_delete(&apds9960);
        i2c_bus_delete(&s_i2c_bus);
        s_i2c_bus = NULL;
        return NULL;
    }

    ESP_LOGI(TAG, "手势传感器初始化完成！");
    return apds9960;
}

/**
 * @brief 反初始化 APDS9960 手势传感器
 */
void deinit_apds9960(void)
{
    if (s_apds9960) {
        apds9960_delete(&s_apds9960);
        s_apds9960 = NULL;
        ESP_LOGI(TAG, "APDS9960 设备已删除");
    }
    if (s_i2c_bus) {
        i2c_bus_delete(&s_i2c_bus);
        s_i2c_bus = NULL;
        ESP_LOGI(TAG, "I2C 总线已删除");
    }
}
