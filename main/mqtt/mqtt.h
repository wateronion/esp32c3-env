#pragma once

/* ============================================================
 *  OneNet MQTT 配置
 *
 *  根据 OneNet 平台版本选择正确的 Broker 地址：
 *
 *  旧版 OneNet（多协议接入）:
 *    - 地址: mqtt.heclouds.com  端口: 6002
 *    - IP:   183.230.40.96      端口: 6002
 *    - 认证: Client ID = 设备名, Username = 产品ID, Password = token
 *
 *  新版 OneNET Studio:
 *    - 地址: studio-mqtt.heclouds.com  端口: 1883
 *    - 认证: Client ID / Username / Password 由平台分配
 *
 *  你的密码格式 (version=2018-10-31&res=products/...) 是旧版 OneNet
 *  的 token 鉴权格式，推荐使用端口 6002。
 * ============================================================ */
#define MQTT_BROKER_URI     "mqtt://mqtt.heclouds.com:6002"
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
