/**
 * @file main.c
 * @brief ESP32-S3 手势识别与模型控制主程序
 * 
 * 功能：
 * 1. 使用 APDS9960 手势传感器控制 LED 亮度
 * 2. 通过 UDP 传输摄像头图像到 PC
 * 3. 通过 Boot 按钮切换控制模式（手势/模型）
 * 4. 通过 UDP 命令接收 PC 端模型推理结果控制 LED
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "img_converters.h"
#include "apds9960.h"
#include "camera.h"
#include "lcd.h"
#include "spi.h"
#include "led.h"

// 日志标签
static const char *TAG = "MAIN";

// WiFi 配置
#define WIFI_SSID      "Crystal"      // WiFi 名称
#define WIFI_PASS      "xixiaini44"   // WiFi 密码

// 网络端口配置
#define UDP_PORT       5555           // 图像传输端口
#define CMD_PORT       5556           // 命令接收端口
#define PC_IP          "192.168.137.1" // PC 端 IP 地址

// 手势控制超时时间（3秒）
#define GESTURE_TO     3000000

// 按钮配置（使用开发板自带的 Boot 按钮）
#define BTN_MODE       GPIO_NUM_0

// 控制模式定义
#define MODE_GESTURE   0              // 手势控制模式
#define MODE_MODEL     1              // 模型控制模式

// 当前控制模式
static int control_mode = MODE_GESTURE;

// 外部变量声明
extern uint8_t *lcd_buf;

// 网络相关变量
static int udp_sock = -1;             // UDP 图像传输套接字
static int cmd_sock = -1;             // UDP 命令接收套接字
static struct sockaddr_in pc_addr;    // PC 端地址

// LED 亮度控制变量
static int pc_brightness = 0;         // PC 模型控制的亮度值
static int64_t gesture_expire = 0;    // 手势控制超时时间戳
static int current_brightness = -1;   // 当前 LED 亮度

/*
设置 LED 亮度
亮度级别 (0=OFF, 1=LOW, 2=MED, 3=HIGH)
 */
static void apply_led(int level)
{
    // 如果亮度没有变化，直接返回
    if (level == current_brightness)
        return;
    
    current_brightness = level;
    led_set_brightness(level);

    // 定义亮度级别名称
    static const char *names[] = {"OFF", "LOW", "MED", "HIGH"};
    ESP_LOGI(TAG, "LED -> %s", names[level]);
}

/**
 * @brief WiFi 事件处理函数
 */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    switch (id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi 启动，开始连接...");
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi 断开连接，重新连接...");
            esp_wifi_connect();
            break;
        case IP_EVENT_STA_GOT_IP: {
            ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
            ESP_LOGI(TAG, "WiFi 连接成功，IP: " IPSTR, IP2STR(&evt->ip_info.ip));
            break;
        }
        default:
            break;
    }
}

/**
 * @brief 初始化 WiFi 连接
 */
static void wifi_init(void)
{
    // 初始化 NVS
    ESP_ERROR_CHECK(nvs_flash_init());
    
    // 初始化网络接口
    ESP_ERROR_CHECK(esp_netif_init());
    
    // 创建默认事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // 创建默认 WiFi STA 接口
    esp_netif_create_default_wifi_sta();

    // 初始化 WiFi 配置
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册 WiFi 事件处理
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    // 配置 WiFi SSID 和密码
    wifi_config_t wifi_cfg = {0};
    strcpy((char *)wifi_cfg.sta.ssid, WIFI_SSID);
    strcpy((char *)wifi_cfg.sta.password, WIFI_PASS);

    // 设置 WiFi 模式为 STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/**
 * @brief 在 LCD 上显示摄像头帧
 * @param fb 摄像头帧缓冲区
 */
static void show_frame(camera_fb_t *fb)
{
    // 计算帧数据大小（RGB565 格式）
    unsigned long bytes = (unsigned long)(fb->width * fb->height * 2);
    
    // 设置 LCD 显示窗口
    lcd_set_window(0, 0, fb->width - 1, fb->height - 1);
    
    // 将帧数据复制到 LCD 缓冲区
    memcpy(lcd_buf, fb->buf, bytes);

    // 分块写入 LCD（每块 11520 字节）
    unsigned long chunk = 11520;
    unsigned long full = bytes / chunk;
    unsigned long rem = bytes % chunk;
    
    for (unsigned long j = 0; j < full; j++) {
        lcd_write_datan(&lcd_buf[j * chunk], chunk);
    }
    if (rem > 0) {
        lcd_write_datan(&lcd_buf[full * chunk], rem);
    }
}

/**
 * @brief 检查 UDP 命令接收套接字
 * 
 * 从 PC 端接收 LED 亮度控制命令
 */
static void check_cmd_socket(void)
{
    uint8_t buf[4];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    
    // 非阻塞方式接收数据
    //参数：
    //cmd_sock：套接字描述符
    //buf：接收缓冲区
    //sizeof(buf)：缓冲区大小
    //MSG_DONTWAIT：非阻塞接收
    //from：客户端地址
    //fromlen：客户端地址长度
    //返回值：接收的字节数
    int n = recvfrom(cmd_sock, buf, sizeof(buf), MSG_DONTWAIT,
                     (struct sockaddr *)&from, &fromlen);
    
    if (n >= 1) {
        // 将字符转换为数字（'0'-'3' -> 0-3）
        int cmd = buf[0] - '0';
        if (cmd >= 0 && cmd <= 3) {
            pc_brightness = cmd;
            ESP_LOGI(TAG, "PC 命令: 亮度=%d", cmd);
        }
    }
}

/**
 * @brief 检查手势控制状态
 * 
 * 处理 APDS9960 手势传感器的输出，控制 LED 亮度
 */
static void check_gesture(void)
{
    // 如果有新的手势命令
    if (g_gesture_override) {
        gesture_expire = esp_timer_get_time() + GESTURE_TO;
        g_gesture_override = 0;
        apply_led(g_gesture_brightness);
        ESP_LOGI(TAG, "手势控制: %d (3秒超时)", g_gesture_brightness);
    }

    // 检查手势控制是否超时
    if (gesture_expire > 0) {
        if (esp_timer_get_time() < gesture_expire) {
            return;  // 还在超时时间内，保持当前亮度
        }
        gesture_expire = 0;
        ESP_LOGI(TAG, "手势控制超时");
    }
}

/**
 * @brief 控制逻辑主循环
 * 
 * 根据当前模式调用对应的控制函数
 */
static void control_tick(void)
{
    if (control_mode == MODE_GESTURE) {
        // 手势控制模式
        check_gesture();
    } else {
        // 模型控制模式
        check_cmd_socket();
        apply_led(pc_brightness);
    }
}

// 按键状态枚举
typedef enum {
    KEY_STATE_IDLE,       // 空闲状态（按键未按下）
    KEY_STATE_DEBOUNCE,   // 消抖状态
    KEY_STATE_PRESSED,    // 按键已按下（等待释放）
} key_state_t;

// 按键状态变量
static key_state_t key_state = KEY_STATE_IDLE;      // 当前按键状态
static int64_t key_debounce_tick = 0;               // 消抖时间戳

/**
 * @brief 非阻塞式按键扫描函数
 * @return 按键编号（1=Boot按钮），0=无按键
 */
static uint8_t key_scan(void)
{
    uint8_t key_num = 0;
    int64_t now = esp_timer_get_time();

    // 读取 Boot 按钮状态（上拉输入，按下为低电平）
    int b = gpio_get_level(BTN_MODE);

    // 状态机处理
    switch (key_state) {
        case KEY_STATE_IDLE:
            // 检测到按键按下（低电平）
            if (b == 0) {
                key_state = KEY_STATE_DEBOUNCE;
                key_debounce_tick = now;
            }
            break;
            
        case KEY_STATE_DEBOUNCE:
            // 20ms 消抖检测
            if (now - key_debounce_tick > 20000) {
                if (b == 0) {
                    // 确认按键按下
                    key_state = KEY_STATE_PRESSED;
                } else {
                    // 抖动，回到空闲状态
                    key_state = KEY_STATE_IDLE;
                }
            }
            break;
            
        case KEY_STATE_PRESSED:
            // 检测按键释放（高电平）
            if (b == 1) {
                key_state = KEY_STATE_IDLE;
                key_num = 1;  // 返回按键编号
            }
            break;
    }

    return key_num;
}

/**
 * @brief 检查按钮状态并处理模式切换
 */
static void check_buttons(void)
{
    uint8_t key = key_scan();
    
    if (key == 1) {
        // 切换控制模式
        control_mode = !control_mode;
        
        if (control_mode == MODE_GESTURE) {
            ESP_LOGI(TAG, ">>> Boot 按钮: 切换到手势控制模式");
        } else {
            ESP_LOGI(TAG, ">>> Boot 按钮: 切换到模型控制模式");
        }
        
        // 重置状态
        apply_led(0);
        pc_brightness = 0;
        gesture_expire = 0;
    }
}

/**
 * @brief 初始化按钮 GPIO
 */
static void buttons_init(void)
{
    gpio_config_t btn_cfg = {
        .intr_type    = GPIO_INTR_DISABLE,  // 禁用中断
        .mode         = GPIO_MODE_INPUT,    // 输入模式
        .pin_bit_mask = (1ULL << BTN_MODE), // Boot 按钮引脚
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_ENABLE, // 启用内部上拉
    };
    gpio_config(&btn_cfg);
    
    // 等待稳定
    vTaskDelay(pdMS_TO_TICKS(50));

    // 输出初始状态
    int lv0 = gpio_get_level(BTN_MODE);
    ESP_LOGI(TAG, "按钮初始化: IO0=%d (1=未按下, Boot Button)", lv0);
}

/**
 * @brief 主程序入口
 */
void app_main(void)
{
    // 初始化 LED 和 PWM
    led_init();
    led_pwm_init();
    
    // 初始化 LCD 并清屏
    lcd_init();
    lcd_clear(BLUE);

    // 初始化手势传感器
    init_apds9960();
    
    // 初始化摄像头
    camera_init();
    
    // 初始化 WiFi
    wifi_init();
    
    // 初始化按钮
    buttons_init();

    // 输出启动信息
    ESP_LOGI(TAG, "启动完成，当前模式: 手势控制");

    // 创建 UDP 图像传输套接字
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        ESP_LOGE(TAG, "创建 UDP 套接字失败");
        return;
    }
    
    // 设置发送缓冲区大小
    int snd = 64 * 1024;
    setsockopt(udp_sock, SOL_SOCKET, SO_SNDBUF, &snd, sizeof(snd));

    // 创建 UDP 命令接收套接字
    cmd_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (cmd_sock < 0) {
        ESP_LOGE(TAG, "创建 CMD 套接字失败");
        return;
    }
    
    // 绑定命令端口
    struct sockaddr_in cmd_addr = {0};
    cmd_addr.sin_family = AF_INET;
    cmd_addr.sin_port = htons(CMD_PORT);
    cmd_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(cmd_sock, (struct sockaddr *)&cmd_addr, sizeof(cmd_addr));

    // 设置 PC 端地址
    memset(&pc_addr, 0, sizeof(pc_addr));
    pc_addr.sin_family = AF_INET;
    pc_addr.sin_port = htons(UDP_PORT);
    pc_addr.sin_addr.s_addr = inet_addr(PC_IP);

    // 输出网络配置信息
    ESP_LOGI(TAG, "UDP 图像传输: %s:%d", PC_IP, UDP_PORT);
    ESP_LOGI(TAG, "UDP 命令接收: :%d", CMD_PORT);
    ESP_LOGI(TAG, "摄像头 240x240 流传输中...");

    uint32_t seq = 0;                    // 帧序号
    TickType_t last_log = xTaskGetTickCount();  // 上次日志时间

    // 主循环
    while (1) {
        // 获取摄像头帧
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 在 LCD 上显示帧
        show_frame(fb);

        // 将帧转换为 JPEG
        uint8_t *jpg_buf = NULL;
        size_t jpg_len = 0;

        if (fmt2jpg(fb->buf, fb->len, fb->width, fb->height,
                    PIXFORMAT_RGB565, 70, &jpg_buf, &jpg_len)) {

            // 构建图像帧头（10字节）
            uint8_t header[10];
            header[0] = 0xFF;  // JPEG 起始标记
            header[1] = 0xD8;
            memcpy(&header[2], &seq, 4);  // 帧序号
            header[6] = (uint8_t)(fb->width & 0xFF);       // 宽度低字节
            header[7] = (uint8_t)((fb->width >> 8) & 0xFF); // 宽度高字节
            header[8] = (uint8_t)(fb->height & 0xFF);      // 高度低字节
            header[9] = (uint8_t)((fb->height >> 8) & 0xFF);// 高度高字节

            // 使用 scatter-gather I/O 发送数据
            struct iovec iov[2];
            iov[0].iov_base = header;
            iov[0].iov_len = 10;
            iov[1].iov_base = jpg_buf + 2;  // 跳过 JPEG 起始标记（已在 header 中）
            iov[1].iov_len = jpg_len - 2;

            struct msghdr msg;
            memset(&msg, 0, sizeof(msg));
            msg.msg_name = &pc_addr;
            msg.msg_namelen = sizeof(pc_addr);
            msg.msg_iov = iov;
            msg.msg_iovlen = 2;

            // 发送到 PC
            sendmsg(udp_sock, &msg, 0);
            free(jpg_buf);
            seq++;
        }

        // 返回帧缓冲区
        esp_camera_fb_return(fb);

        // 检查按钮状态
        check_buttons();
        
        // 执行控制逻辑
        control_tick();

        // 每 5 秒输出一次统计信息
        TickType_t now = xTaskGetTickCount();
        if (now - last_log > pdMS_TO_TICKS(5000)) {
            ESP_LOGI(TAG, "已发送 %lu 帧, LED亮度=%d", (unsigned long)seq, current_brightness);
            last_log = now;
            seq = 0;
        }

        // 延时约 33ms（约 30fps）
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}
