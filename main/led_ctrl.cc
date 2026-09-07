#include "led_ctrl.h"

led_ctrl::led_ctrl()
{
    led_state_ = 0;
}

led_ctrl::~led_ctrl()
{
}

esp_err_t led_ctrl::led_init()
{
    // 定义 GPIO 配置结构体，设置各配置项
    gpio_config_t gpio = {
        .pin_bit_mask = 1 << LED_PIN,          // 位掩码：选中 GPIO10 引脚（bit10 置 1）
        .mode = GPIO_MODE_OUTPUT,              // 模式：推挽输出模式
        .pull_up_en = GPIO_PULLUP_DISABLE,     // 上拉：禁用内部上拉电阻
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // 下拉：禁用内部下拉电阻
        .intr_type = GPIO_INTR_DISABLE,        // 中断：不使用中断（禁用 GPIO 中断）
    };
    // gpio的配置：将上述配置写入硬件寄存器，完成 GPIO 初始化
    gpio_config(&gpio);

    // 灯为熄灭状态：输出高电平（本工程中低电平点亮、高电平熄灭）
    gpio_set_level(LED_PIN, 1);

    // 初始化LED状态为熄灭
    led_state_ = 0;

    return ESP_OK;
}

esp_err_t led_ctrl::led_on()
{
    led_state_ = 1;
    gpio_set_level(LED_PIN, 0);
    return ESP_OK;
}

esp_err_t led_ctrl::led_off()
{
    led_state_ = 0;
    gpio_set_level(LED_PIN, 1);
    return ESP_OK;
}

uint32_t led_ctrl::led_get_state()
{
    return led_state_;
}

esp_err_t led_ctrl::led_toggle()
{
    led_state_ = !led_state_;                    // 翻转状态标志
    gpio_set_level(LED_PIN, led_state_ ? 0 : 1); // 亮：输出低电平；灭：输出高电平
    return ESP_OK;
}