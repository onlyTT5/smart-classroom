#ifndef __LED_CTRL_H__
#define __LED_CTRL_H__

#include "driver/gpio.h"

#define LED_PIN GPIO_NUM_10        // 宏定义：LED 所连接的 GPIO 引脚号（GPIO10）
#include <esp_err.h>

class led_ctrl{

public:

    // 构造函数
    led_ctrl();

    // 析构函数
    ~led_ctrl();

    //初始化led
    esp_err_t led_init();


    //亮灯
    esp_err_t led_on();

    //灭灯
    esp_err_t led_off();

    //获取灯的状态
    uint32_t led_get_state();

    esp_err_t led_toggle();

    uint32_t led_state_;

};
















#endif