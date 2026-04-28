#pragma once

#include "driver/gpio.h"

#define LED_LEFT 13
#define LED_RIGHT 12

#define LED_LEFT_ON gpio_set_level(LED_LEFT, 1)
#define LED_LEFT_OFF gpio_set_level(LED_LEFT, 0)
#define LED_RIGHT_ON gpio_set_level(LED_RIGHT, 1)
#define LED_RIGHT_OFF gpio_set_level(LED_RIGHT, 0)

void led_init(void);