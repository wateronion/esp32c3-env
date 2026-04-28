/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "wifi.h"

void app_main(void)
{
    printf("Hello world!\n");
    led_init();

    xTaskCreate(wifi_task, "wifi_task", 4096, NULL, 5, NULL);
    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
