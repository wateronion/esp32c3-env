#pragma once

/* ============================================================
 *  OneNet MQTT 配置
 *
 *  mqtt.heclouds.com 是域名负载均衡，每次解析可能返回不同 IP，
 *  部分 IP 的 1883 端口可能不可达，导致间歇性连接失败。
 *  建议直接用固定 IP，避免 DNS 不稳定的影响。
 *
 *  备选地址:
 *    - 域名:         mqtt://mqtt.heclouds.com:1883
 *    - IP 直连:      mqtt://183.230.40.96:1883      (推荐, 当前使用)
 * ============================================================ */
#define MQTT_BROKER_URI     "mqtt://183.230.40.96:1883"
#define MQTT_CLIENT_ID      "LAODING1"
#define MQTT_USERNAME       "Uo4T0ad9Y8"
#define MQTT_PASSWORD       "version=2018-10-31&res=products%2FUo4T0ad9Y8%2Fdevices%2FLAODING1&et=1780024480&method=md5&sign=wZbKTh5x%2B2CJzWMjEs1UAA%3D%3D"

/* MQTT 缓冲区与超时配置 */
#define MQTT_TASK_STACK_SIZE 5120
#define MQTT_TASK_PRIORITY   5

/*
 * mqtt_app_start()     — 初始化并启动 MQTT 客户端连接
 * mqtt_app_publish()   — 向指定 topic 发布数据（仅在已连接时发送）
 * mqtt_app_subscribe() — 订阅指定 topic
 * mqtt_app_is_connected() — 查询 MQTT 是否已连接
 * mqtt_task()          — MQTT 任务入口（等待 WiFi 就绪后连接 MQTT）
 */
void mqtt_app_start(void);
void mqtt_app_publish(const char *topic, const char *data, int len);
void mqtt_app_subscribe(const char *topic, int qos);
int mqtt_app_is_connected(void);
void mqtt_task(void *pvParameters);
