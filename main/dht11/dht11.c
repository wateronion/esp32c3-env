/*
 * DHT11 温湿度传感器驱动
 *
 * 单总线协议，使用 GPIO 模拟时序。
 *
 * --- 通信时序 ---
 *
 *   主机发送起始信号（推挽输出）：
 *     ┌─────┐        ┌──────────
 *     │     │  20ms  │  30us
 *     └──────┘───────┘
 *     拉低         拉高
 *
 *   DHT11 响应（切换输入，依赖上拉）：
 *     ┌────────┐    ┌────────┐
 *     │ 80us   │    │ 80us   │
 *     └────────┘────┘────────┘
 *     拉低响应    拉高就绪
 *
 *   数据位 "0"：
 *     ┌─────┐  ┌──────────────
 *     │50us │  │ 26us
 *     └─────┘──┘
 *
 *   数据位 "1"：
 *     ┌─────┐  ┌──────────────
 *     │50us │  │ 70us
 *     └─────┘──┘
 *
 *   共 40 bit（5 字节）：湿整 + 湿小 + 温整 + 温小 + 校验
 *   校验 = (byte0 + byte1 + byte2 + byte3) & 0xFF
 *
 * --- 硬件注意事项 ---
 *
 *   DHT11 DATA 引脚必须接 4.7kΩ 上拉电阻到 3.3V！
 *   ESP32-C3 内部上拉约 45kΩ，不足以满足 DHT11 时序要求。
 *   接线：DHT11(VCC→3.3V, GND→GND, DATA→GPIO8)
 *         DATA 与 VCC 之间跨接 4.7kΩ 电阻
 * ============================================================ */

#include <stdio.h>
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "dht11.h"

static const char *TAG = "dht11";

/*
 * 微秒级阻塞延时
 */
static inline void delay_us(uint32_t us)
{
    esp_rom_delay_us(us);
}

/*
 * 推挽输出模式
 * 发送起始信号时需要快速拉高拉低，不能用开漏。
 */
static inline void dht11_set_output(int level)
{
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT11_GPIO, level);
}

/*
 * 切换为输入模式（读取 DHT11 数据时需要上拉）
 */
static inline void dht11_set_input(void)
{
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(DHT11_GPIO, GPIO_PULLUP_ONLY);
}

/*
 * 读取引脚电平
 */
static inline int dht11_read_pin(void)
{
    return gpio_get_level(DHT11_GPIO);
}

/*
 * 等待引脚变为指定电平，超时返回 -1
 */
static int dht11_wait_for(int level, int max_us)
{
    int waited = 0;
    while (dht11_read_pin() != level) {
        if (waited >= max_us) {
            return -1;
        }
        delay_us(1);
        waited++;
    }
    return waited;
}

/*
 * 读取一个数据位
 *
 * 每位时序：50us 低电平 + 26~70us 高电平
 *   高电平 26us ≈ bit 0
 *   高电平 70us ≈ bit 1
 * 在 40us 处采样可区分二者。
 */
static int dht11_read_bit(void)
{
    /* 等待引脚变高（数据位起始） */
    if (dht11_wait_for(1, 100) < 0) {
        return -1;
    }
    /* 延时 40us 后采样 */
    delay_us(40);
    if (dht11_read_pin() == 1) {
        /* 还在高电平 → bit 1，等待剩余时间 */
        dht11_wait_for(0, 100);
        return 1;
    } else {
        /* 已变低 → bit 0 */
        return 0;
    }
}

/*
 * 读取一个字节（8 bit），高位在前
 */
static int dht11_read_byte(void)
{
    int val = 0;
    for (int i = 0; i < 8; i++) {
        int bit = dht11_read_bit();
        if (bit < 0) {
            return -1;
        }
        val = (val << 1) | bit;
    }
    return val;
}

/*
 * 初始化 DHT11 引脚
 */
void dht11_init(void)
{
    dht11_set_input();
    ESP_LOGI(TAG, "DHT11 initialized on GPIO%d (pull-up 4.7kΩ required)", DHT11_GPIO);
}

/*
 * 读取温湿度
 *
 * 返回值 dht11_data_t：
 *   .ret = 0  成功，temperature / humidity 有效
 *   .ret = -1 失败，数据不可用
 *
 * 注意：每次读取间隔需大于 1 秒，否则 DHT11 不响应。
 */
dht11_data_t dht11_read(void)
{
    dht11_data_t result = { .ret = -1 };

    /* 关闭中断，避免时序被中断打乱 */
    taskDISABLE_INTERRUPTS();

    /* ---------- 1. 发送起始信号（推挽输出） ---------- */
    dht11_set_output(0);                      /* 拉低至少 18ms */
    delay_us(20000);
    dht11_set_output(1);                      /* 拉高 20~40us   */
    delay_us(30);
    dht11_set_input();                        /* 切换输入，等待 DHT11 响应 */

    /* ---------- 2. 等待 DHT11 响应 ---------- */
    /* 响应信号：拉低 80us + 拉高 80us */
    if (dht11_wait_for(0, 200) < 0) {
        taskENABLE_INTERRUPTS();
        ESP_LOGE(TAG, "no response low");
        return result;
    }
    if (dht11_wait_for(1, 200) < 0) {
        taskENABLE_INTERRUPTS();
        ESP_LOGE(TAG, "no response high");
        return result;
    }

    /* ---------- 3. 读取 40 bit 数据 ---------- */
    int bytes[5];
    for (int i = 0; i < 5; i++) {
        bytes[i] = dht11_read_byte();
        if (bytes[i] < 0) {
            taskENABLE_INTERRUPTS();
            ESP_LOGE(TAG, "byte %d read failed", i);
            return result;
        }
    }

    taskENABLE_INTERRUPTS();

    /* ---------- 4. 校验 ---------- */
    uint8_t checksum = (bytes[0] + bytes[1] + bytes[2] + bytes[3]) & 0xFF;
    if (checksum != (uint8_t)bytes[4]) {
        ESP_LOGE(TAG, "checksum failed: %02X != %02X", checksum, bytes[4]);
        return result;
    }

    /* ---------- 5. 解析数据 ---------- */
    result.humidity    = (float)bytes[0] + (float)bytes[1] / 10.0f;
    result.temperature = (float)bytes[2] + (float)bytes[3] / 10.0f;
    result.ret = 0;

    ESP_LOGI(TAG, "temp=%.1f humi=%.1f", result.temperature, result.humidity);
    return result;
}
