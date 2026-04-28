/*
 * WiFi 驱动模块
 *
 * 基于 ESP-IDF 官方 WiFi API 实现 Station 模式联网。
 *
 * --- 驱动流程 ---
 *
 *   wifi_task()
 *       │
 *       ├── wifi_init()
 *       │       ├── nvs_flash_init()        -- 初始化 NVS（WiFi 需用 NVS 存储固件参数）
 *       │       ├── esp_netif_init()        -- 初始化 TCP/IP 网络接口层
 *       │       ├── esp_event_loop_create_default() -- 创建默认事件循环
 *       │       ├── esp_netif_create_default_wifi_sta() -- 创建 STA 网络接口
 *       │       └── esp_wifi_init()         -- 初始化 WiFi 驱动
 *       │
 *       ├── wifi_connect()
 *       │       ├── xEventGroupCreate()     -- 创建事件组，用于同步连接结果
 *       │       ├── 注册事件回调（WIFI_EVENT / IP_EVENT）
 *       │       ├── esp_wifi_set_mode()     -- 设为 Station 模式
 *       │       ├── esp_wifi_set_config()   -- 写入 SSID / 密码
 *       │       ├── esp_wifi_start()        -- 启动 WiFi
 *       │       └── xEventGroupWaitBits()   -- 阻塞等待连接成功或失败（portMAX_DELAY）
 *       │
 *       └── while(1) 循环
 *               └── 每 5 秒打印一次连接状态
 *
 * --- 事件回调说明 ---
 *
 *   WIFI_EVENT, STA_START        → 驱动已就绪，发起连接 esp_wifi_connect()
 *   WIFI_EVENT, STA_DISCONNECTED → 断开/连接失败，自动重连（最多 WIFI_MAX_RETRY 次）
 *   IP_EVENT,   STA_GOT_IP       → 获取到 IP，置位 CONNECTED_BIT 通知上层任务
 *
 * --- FreeRTOS 事件组同步机制 ---
 *
 *   wifi_connect() 使用事件组阻塞等待连接结果：
 *     - 收到 IP 后事件回调置位 CONNECTED_BIT → wifi_connect() 返回成功
 *     - 重连耗尽后事件回调置位 FAIL_BIT     → wifi_connect() 返回失败
 *   这种设计让调用方可以简单地同步等待连接完成，无需自行管理状态。
 * ============================================================ */

#include <string.h>
#include "wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

static const char *TAG = "wifi";

/* 事件组句柄，用于在事件回调和 wifi_connect() 之间同步连接状态 */
static EventGroupHandle_t s_wifi_event_group;

/* 当前已连续重连的次数，达到 WIFI_MAX_RETRY 后不再尝试 */
static int s_retry_count;

/*
 * WiFi 事件回调
 *
 * 注册到默认事件循环，监听两类事件：
 *   1. WIFI_EVENT     — WiFi 驱动状态变化（启动、断开等）
 *   2. IP_EVENT       — TCP/IP 协议栈事件（获取到 IP 等）
 *
 * arg          : 注册时传入的自定义参数（本模块未使用）
 * event_base   : 事件类型（WIFI_EVENT / IP_EVENT）
 * event_id     : 具体事件编号
 * event_data   : 事件相关数据，类型取决于 event_base + event_id
 */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    /* ---------- WiFi 驱动事件 ---------- */

    /* WiFi 驱动已启动（Station 模式），开始连接热点 */
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    /* 与热点断开连接（可能原因：信号弱、热点关闭、认证失败等） */
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_MAX_RETRY) {
            /* 未超过最大重连次数，继续尝试 */
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            /* 重连耗尽，通知 wifi_connect() 连接失败 */
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    }
    /* ---------- IP 事件 ---------- */

    /* 成功从 DHCP 服务器获取到 IP 地址，代表 WiFi 连接完全就绪 */
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;                              /* 重置重连计数   */
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT); /* 通知连接成功 */
    }
}

/*
 * WiFi 初始化
 *
 * 按顺序完成以下步骤（不可颠倒）：
 *   1. NVS 初始化     — WiFi 固件需要 NVS 存储校准参数
 *   2. TCP/IP 初始化  — 启动 lwIP 协议栈
 *   3. 事件循环创建    — 用于异步通知 WiFi/网络事件
 *   4. 创建 STA 接口  — 为 Station 模式创建网络接口
 *   5. WiFi 驱动初始化 — 加载并启动 WiFi 驱动
 *
 * 注意：如果 NVS 分区损坏或版本不匹配，会先擦除再重新初始化。
 */
void wifi_init(void)
{
    /* 1. 初始化 NVS（Non-Volatile Storage）—— WiFi 固件依赖 NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS 分区已满或版本变化，擦除后重新初始化 */
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 2. 初始化 TCP/IP 网络接口层（lwIP） */
    ESP_ERROR_CHECK(esp_netif_init());

    /* 3. 创建默认事件循环——WiFi 和 IP 事件将通过此循环分发 */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 4. 为 Station 模式创建默认网络接口 */
    esp_netif_create_default_wifi_sta();

    /* 5. 初始化 WiFi 驱动（使用默认配置） */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
}

/*
 * 连接 WiFi 热点（阻塞版）
 *
 * 工作流程：
 *   1. 创建事件组，用于同步事件回调的结果
 *   2. 向默认事件循环注册事件回调（WIFI_EVENT + IP_EVENT）
 *   3. 配置 SSID / 密码 / 认证方式（WPA2-PSK）
 *   4. 设置 WiFi 模式为 Station 并启动
 *   5. 阻塞等待连接结果（portMAX_DELAY = 一直等，直到成功或重连耗尽）
 *
 * 返回时连接状态已确定：
 *   - CONNECTED_BIT 置位 → 连接成功，可立即使用网络
 *   - FAIL_BIT 置位      → 重连耗尽，需检查 SSID/密码/热点是否正常
 */
void wifi_connect(void)
{
    /* 1. 创建事件组 */
    s_wifi_event_group = xEventGroupCreate();

    /* 2. 注册事件回调
     *    使用 esp_event_handler_instance_register 而非旧版 esp_event_handler_register，
     *    注册时返回实例句柄，后续可取消注册以精确管理生命周期 */
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,       /* 监听所有 WiFi 事件 */
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    /* 3. 配置 WiFi Station 参数 */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,   /* 至少需要 WPA2 加密 */
        },
    };

    /* 4. 设置模式、写入配置、启动 WiFi */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to %s...", WIFI_SSID);

    /* 5. 阻塞等待连接结果
     *    xEventGroupWaitBits 在至少一个请求的位被置位时返回。
     *    pdFALSE × 2 = 不清除标志位，等待所有指定位的任意一个。
     *    portMAX_DELAY = 无限等待，直到成功或失败。 */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to %s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "failed to connect to %s", WIFI_SSID);
    }
}

/*
 * 断开并停止 WiFi
 */
void wifi_disconnect(void)
{
    esp_wifi_disconnect();
    esp_wifi_stop();
}

/*
 * 查询 WiFi 连接状态
 *
 * 返回值:
 *   0 — 未连接
 *   1 — 已连接（CONNECTED_BIT 置位，已获取 IP）
 */
int wifi_is_connected(void)
{
    if (s_wifi_event_group == NULL) {
        return 0;
    }
    return xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT;
}

/*
 * 获取 WiFi 事件组句柄
 *
 * 外部任务可通过此句柄等待 WiFi 状态变化，例如：
 *   xEventGroupWaitBits(wifi_get_event_group(), WIFI_CONNECTED_BIT, ...);
 * 从而在 WiFi 连接后才开始执行网络 I/O 任务。
 */
EventGroupHandle_t wifi_get_event_group(void)
{
    return s_wifi_event_group;
}

/*
 * WiFi 任务入口
 *
 * 这是推荐的 WiFi 使用方式——一个独立的任务处理 WiFi 全生命周期：
 *
 *   1. 调用 wifi_init()    初始化驱动（NFS → netif → 事件循环 → WiFi）
 *   2. 调用 wifi_connect() 连接热点（阻塞直到成功或重连耗尽）
 *   3. 每隔 5 秒打印一次连接状态
 *
 * 在 main.c 中通过 xTaskCreate 创建此任务即可：
 *   xTaskCreate(wifi_task, "wifi_task", 4096, NULL, 5, NULL);
 */
void wifi_task(void *pvParameters)
{
    wifi_init();
    wifi_connect();

    while (1)
    {
        if (wifi_is_connected())
        {
            printf("WiFi is connected\n");
        }
        else
        {
            printf("WiFi is disconnected\n");
        }
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
