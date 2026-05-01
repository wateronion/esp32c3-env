/*
 * MQTT 驱动模块
 *
 * 基于 ESP-MQTT (esp_mqtt) 实现与 OneNet 物联网平台的连接。
 *
 * --- 工作流程 ---
 *
 *   mqtt_task()
 *       │
 *       ├── 等待 WiFi 连接就绪（轮询 wifi_is_connected()）
 *       │
 *       └── mqtt_app_start()
 *               ├── esp_mqtt_client_init()      -- 创建 MQTT 客户端
 *               ├── esp_mqtt_client_register_event() -- 注册事件回调
 *               └── esp_mqtt_client_start()     -- 发起连接
 *
 * --- 事件回调说明 ---
 *
 *   MQTT_EVENT_CONNECTED      → 已连接 Broker，可订阅 topic
 *   MQTT_EVENT_DISCONNECTED   → 与 Broker 断开，库会自动重连
 *   MQTT_EVENT_DATA           → 收到订阅的 topic 消息
 *   MQTT_EVENT_ERROR          → 发生错误
 *
 * --- OneNet MQTT 地址 ---
 *
 *   旧版 : mqtt.heclouds.com:6002  （你的 token 格式对应此版本）
 *   新版 : studio-mqtt.heclouds.com:1883
 *   认证方式：Client ID = 设备名称, Username = 产品 ID, Password = token
 * ============================================================ */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mqtt_client.h"
#include "mqtt.h"
#include "wifi.h"
#include "dht11.h"

/* 测试模式：DHT11 读取失败时生成模拟数据 */
#define TEST_HUMI_MIN       40
#define TEST_HUMI_MAX       80
#define TEST_TEMP_MIN       20.0f
#define TEST_TEMP_MAX       30.0f

static const char *TAG = "mqtt";

/* MQTT 客户端全局句柄，供 publish / subscribe 使用 */
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

/* MQTT 连接状态标志，由事件回调更新，供 publish / 外部任务查询 */
static int s_mqtt_connected = 0;

/*
 * MQTT 事件回调
 *
 * 由 ESP-MQTT 库在客户端状态变化或收到数据时自动调用。
 * 所有操作（订阅、打印消息、错误处理）均在此回调中完成。
 *
 * handler_args : 注册时传入的自定义参数（未使用）
 * base         : 事件基类（固定为 MQTT_EVENTS）
 * event_id     : 具体事件 ID
 * event_data   : 指向 esp_mqtt_event_t 结构体，包含会话相关信息
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {

    /* ---------- 已连接 Broker ---------- */
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected to OneNet");
        s_mqtt_connected = 1;
        /* 订阅属性下发回复 topic */
        mqtt_app_subscribe("$sys/" MQTT_USERNAME "/" MQTT_CLIENT_ID "/thing/property/post/reply", 1);
        break;

    /* ---------- 与 Broker 断开 ---------- */
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        s_mqtt_connected = 0;
        break;

    /* ---------- 收到订阅的 topic 消息 ---------- */
    case MQTT_EVENT_DATA:
        printf("TOPIC: %.*s\r\n", event->topic_len, event->topic);
        printf("DATA: %.*s\r\n", event->data_len, event->data);
        break;

    /* ---------- 发布消息完成 ---------- */
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT message published (msg_id=%d)", event->msg_id);
        break;

    /* ---------- 发生错误 ---------- */
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error, esp-tls code=%d", event->error_handle->esp_tls_last_esp_err);
        break;

    default:
        break;
    }
}

/*
 * 启动 MQTT 客户端
 *
 * 配置 Broker 地址、端口、认证信息，然后发起连接。
 * 连接成功后通过事件回调通知。
 */
void mqtt_app_start(void)
{
    /* 如果已有客户端则先销毁 */
    if (s_mqtt_client != NULL) {
        esp_mqtt_client_destroy(s_mqtt_client);
    }

    /* 配置 MQTT 客户端参数 */
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.client_id = MQTT_CLIENT_ID,
        .credentials.username = MQTT_USERNAME,
        .credentials.authentication.password = MQTT_PASSWORD,
    };

    /* 初始化客户端 */
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT client init failed");
        return;
    }

    /* 注册事件回调 */
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);

    /* 启动连接（非阻塞，连接结果通过事件回调通知） */
    esp_mqtt_client_start(s_mqtt_client);
    ESP_LOGI(TAG, "MQTT client started, connecting to %s ...", MQTT_BROKER_URI);
}

/*
 * 发布消息到指定 topic
 *
 * topic : 主题字符串（如 "$sys/Uo4T0ad9Y8/LAODING1/thing/property/post"）
 * data  : 消息内容
 * len   : 数据长度（传 0 则自动计算字符串长度）
 */
void mqtt_app_publish(const char *topic, const char *data, int len)
{
    if (s_mqtt_client == NULL || !s_mqtt_connected) {
        ESP_LOGW(TAG, "MQTT not connected, skip publish");
        return;
    }

    int msg_id;
    if (len == 0) {
        msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, data, 0, 1, 0);
    } else {
        msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, data, len, 1, 0);
    }
    ESP_LOGI(TAG, "publish to %s (msg_id=%d)", topic, msg_id);
}

/*
 * 订阅指定 topic
 *
 * topic : 主题
 * qos   : 服务质量等级（0/1/2）
 */
void mqtt_app_subscribe(const char *topic, int qos)
{
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return;
    }

    int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, topic, qos);
    ESP_LOGI(TAG, "subscribe to %s (msg_id=%d)", topic, msg_id);
}

int mqtt_app_is_connected(void)
{
    return s_mqtt_connected;
}

/*
 * MQTT 任务入口
 *
 * 工作流程：
 *   1. 通过事件组等待 WiFi 连接就绪（比轮询更高效）
 *   2. WiFi 就绪后调用 mqtt_app_start() 连接 OneNet Broker
 *   3. 每 10 秒推送一次数据
 */
void mqtt_task(void *pvParameters)
{
    /* 等待 WiFi 事件组创建完成并连接就绪 */
    ESP_LOGI(TAG, "waiting for WiFi...");
    while (1) {
        EventGroupHandle_t wifi_group = wifi_get_event_group();
        if (wifi_group != NULL) {
            /* 事件组已存在，阻塞等待连接成功 */
            xEventGroupWaitBits(wifi_group, WIFI_CONNECTED_BIT,
                                pdFALSE, pdFALSE, portMAX_DELAY);
            break;
        }
        /* 事件组尚未创建（wifi_connect 还没执行到），短暂等待 */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "WiFi connected, starting MQTT...");

    /* 启动 MQTT 连接 */
    mqtt_app_start();

    /* 周期性推送温湿度数据到 OneNet */
    char json[256];
    while (1) {
        /* 读取 DHT11 温湿度 */
        dht11_data_t dht = dht11_read();

        if (mqtt_app_is_connected()) {
            if (dht.ret == 0) {
                /* DHT11 读取成功，填入实际数据 */
                snprintf(json, sizeof(json),
                         "{\"id\":\"123\",\"version\":\"1.0\",\"params\":{"
                         "\"humi\":{\"value\":%d},"
                         "\"temp\":{\"value\":%.1f}}}",
                         (int)dht.humidity, dht.temperature);
            } else {
                /* DHT11 读取失败，使用测试模拟数据 */
                int test_humi = TEST_HUMI_MIN + esp_random() % (TEST_HUMI_MAX - TEST_HUMI_MIN + 1);
                float test_temp = TEST_TEMP_MIN + (esp_random() % 101) * 0.1f;
                snprintf(json, sizeof(json),
                         "{\"id\":\"123\",\"version\":\"1.0\",\"params\":{"
                         "\"humi\":{\"value\":%d},"
                         "\"temp\":{\"value\":%.1f}}}",
                         test_humi, test_temp);
            }
            mqtt_app_publish("$sys/" MQTT_USERNAME "/" MQTT_CLIENT_ID "/thing/property/post",
                            json, 0);
        } else {
            ESP_LOGW(TAG, "MQTT not connected, wait for reconnect...");
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
