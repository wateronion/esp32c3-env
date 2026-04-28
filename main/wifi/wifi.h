#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/* ============================================================
 *  WiFi 配置（使用前请修改为你的热点信息）
 * ============================================================ */
#define WIFI_SSID       "xiaomi14"       /* 目标 WiFi 热点名称 */
#define WIFI_PASS       "1779382977"     /* WiFi 密码           */

/* ============================================================
 *  重连参数
 * ============================================================ */
#define WIFI_MAX_RETRY  5                /* 断开后最大重连次数  */

/* ============================================================
 *  事件标志位
 *
 *  通过 FreeRTOS 事件组（Event Group）向外部任务通知 WiFi 状态。
 *  wifi_connect() 阻塞等待这两个标志位之一被置位。
 * ============================================================ */
#define WIFI_CONNECTED_BIT  BIT0         /* 连接成功（已获取 IP）*/
#define WIFI_FAIL_BIT       BIT1         /* 重连耗尽，连接失败    */

/*
 * wifi_init()   - 初始化 NVS、网络接口、事件循环、WiFi 驱动
 * wifi_connect()- 配置 SSID/密码并以 Station 模式连接（阻塞等待结果）
 * wifi_disconnect() - 断开并停止 WiFi
 * wifi_is_connected() - 查询当前连接状态（0=未连接, 1=已连接）
 * wifi_get_event_group() - 获取事件组句柄，可用于外部任务监听 WiFi 状态
 * wifi_task()   - 完整的 WiFi 任务入口：init → connect → 周期性打印状态
 */
void wifi_init(void);
void wifi_connect(void);
void wifi_disconnect(void);
int wifi_is_connected(void);
EventGroupHandle_t wifi_get_event_group(void);
void wifi_task(void *pvParameters);
