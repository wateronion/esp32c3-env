#pragma once

#include "driver/gpio.h"

/* DHT11 数据引脚（建议使用 GPIO8，该引脚未被其他外设占用） */
#define DHT11_GPIO          GPIO_NUM_8

/* DHT11 数据结构体 */
typedef struct
{
    float temperature;
    float humidity;
    int   ret;             /* 0 = 成功，-1 = 读取失败 */
} dht11_data_t;

/*
 * dht11_init()       — 初始化 DHT11 引脚
 * dht11_read()       — 读取温湿度（阻塞约 20ms），返回结构体
 */
void dht11_init(void);
dht11_data_t dht11_read(void);
