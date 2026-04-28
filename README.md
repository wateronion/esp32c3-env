# ESP32-C3 Project

## WiFi 模块使用说明

### 配置 WiFi 连接

编辑 `main/wifi/wifi.h`，修改以下宏定义：

```c
#define WIFI_SSID "your_ssid"   // WiFi 热点名称
#define WIFI_PASS "your_password" // WiFi 密码
```

### 在项目中启用 WiFi

在 `main.c` 中创建任务启动 WiFi：

```c
#include "wifi.h"

void app_main(void)
{
    xTaskCreate(wifi_task, "wifi_task", 4096, NULL, 5, NULL);
}
```

`wifi_task` 会自动完成以下流程：
1. 初始化 NVS 存储
2. 创建网络接口
3. 以 Station 模式连接配置的 WiFi 热点
4. 连接成功后每 5 秒打印一次连接状态

### API 说明

| 函数 | 说明 |
|------|------|
| `wifi_init()` | 初始化 NVS、网络接口和 WiFi 驱动 |
| `wifi_connect()` | 连接 WiFi 热点（阻塞等待结果） |
| `wifi_disconnect()` | 断开 WiFi 连接 |
| `wifi_is_connected()` | 返回连接状态（0=未连接, 1=已连接） |
| `wifi_get_event_group()` | 获取 WiFi 事件组句柄 |
| `wifi_task()` | 完整的 WiFi 任务入口（内含 init + connect + 状态循环） |

### 注意事项

- 确保 `sdkconfig` 中已启用 WiFi 相关配置（首次构建时 ESP-IDF 会自动生成）
- 如需调整，运行 `idf.py menuconfig` 进行配置
- 默认重连次数为 5 次（`WIFI_MAX_RETRY`），可自行修改
