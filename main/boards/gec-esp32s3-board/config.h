
#ifndef _BOARD_CONFIG_H_ // 防止头文件重复包含的宏定义
#define _BOARD_CONFIG_H_ // 定义配置头文件标识符

#include <driver/gpio.h> // 包含ESP32 GPIO驱动头文件，提供GPIO_NUM_xx等宏定义

// LCD驱动芯片选择配置
// #define LCD_DRIVER_ST7789V2  // 注释掉ST7789V2驱动，当前不使用
#define LCD_DRIVER_ST7796 // 启用ST7796 LCD驱动芯片支持

//#define TP_DRIVER_FT5X06 // 启用ft5x06触摸面板驱动芯片支持
#define TP_DRIVER_FT6X36 // 启用FT6X36触摸面板驱动芯片支持

// 音频采样率配置
#define AUDIO_INPUT_SAMPLE_RATE 24000  // 音频输入采样率：24kHz
#define AUDIO_OUTPUT_SAMPLE_RATE 24000 // 音频输出采样率：24kHz

#define AUDIO_INPUT_REFERENCE true // 音频输入参考电压使能

// I2S音频接口引脚配置
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_38 // I2S主时钟引脚：GPIO38
#define AUDIO_I2S_GPIO_WS GPIO_NUM_13   // I2S字选择(左右声道)引脚：GPIO13
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_14 // I2S位时钟引脚：GPIO14
#define AUDIO_I2S_GPIO_DIN GPIO_NUM_12  // I2S数据输入引脚：GPIO12(从麦克风到ESP32)
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_45 // I2S数据输出引脚：GPIO45(从ESP32到扬声器)

// 音频编解码器配置
#define AUDIO_CODEC_USE_PCA9557                           // 启用PCA9557 GPIO扩展芯片控制音频功放
#define AUDIO_CODEC_I2C_SDA_PIN GPIO_NUM_1                // 音频编解码器I2C SDA引脚：GPIO1
#define AUDIO_CODEC_I2C_SCL_PIN GPIO_NUM_2                // 音频编解码器I2C SCL引脚：GPIO2
#define AUDIO_CODEC_ES8311_ADDR ES8311_CODEC_DEFAULT_ADDR // ES8311 DAC默认地址
#define AUDIO_CODEC_ES7210_ADDR 0x82                      // ES7210 ADC设备地址：0x82

// 按钮和LED引脚配置
#define BUILTIN_LED_GPIO GPIO_NUM_48        // 板载LED引脚：GPIO48
#define BOOT_BUTTON_GPIO GPIO_NUM_0         // 启动按钮引脚：GPIO0
#define VOLUME_UP_BUTTON_GPIO GPIO_NUM_NC   // 音量加按钮引脚：未连接(NC)
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC // 音量减按钮引脚：未连接(NC)

// ST7789V2 LCD显示屏配置(条件编译，当前未启用)
#ifdef LCD_DRIVER_ST7789V2
#define DISPLAY_WIDTH 320      // 显示屏宽度：320像素
#define DISPLAY_HEIGHT 240     // 显示屏高度：240像素
#define DISPLAY_MIRROR_X true  // X轴镜像：启用(水平翻转)
#define DISPLAY_MIRROR_Y false // Y轴镜像：禁用(垂直不翻转)
#define DISPLAY_SWAP_XY true   // 交换XY坐标：启用(旋转90度)

#define DISPLAY_OFFSET_X 0 // 显示偏移X：0像素
#define DISPLAY_OFFSET_Y 0 // 显示偏移Y：0像素
#endif

// ST7796 LCD显示屏配置(当前启用的配置)
#ifdef LCD_DRIVER_ST7796
#define DISPLAY_WIDTH 480     // 显示屏宽度：480像素
#define DISPLAY_HEIGHT 320    // 显示屏高度：320像素
#define DISPLAY_MIRROR_X true // X轴镜像：启用(水平翻转)
#define DISPLAY_MIRROR_Y true // Y轴镜像：启用(垂直翻转)
#define DISPLAY_SWAP_XY true  // 交换XY坐标：启用(旋转90度)

#define DISPLAY_OFFSET_X 0 // 显示偏移X：0像素
#define DISPLAY_OFFSET_Y 0 // 显示偏移Y：0像素
#endif

// 显示屏背光配置
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_42    // 背光控制引脚：GPIO42
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT true // 背光输出反转：启用(低电平有效)

/* Camera pins - 摄像头引脚配置 */
#define CAMERA_PIN_PWDN -1  // 摄像头电源关闭引脚：未使用(-1)
#define CAMERA_PIN_RESET -1 // 摄像头复位引脚：未使用(-1)
#define CAMERA_PIN_XCLK 5   // 摄像头XCLK时钟引脚：GPIO5
#define CAMERA_PIN_SIOD 1   // 摄像头SCCB SDA数据引脚：GPIO1
#define CAMERA_PIN_SIOC 2   // 摄像头SCCB SCL时钟引脚：GPIO2

// 摄像头数据引脚配置(D0-D7)
#define CAMERA_PIN_D7 9  // 摄像头数据位7引脚：GPIO9
#define CAMERA_PIN_D6 4  // 摄像头数据位6引脚：GPIO4
#define CAMERA_PIN_D5 6  // 摄像头数据位5引脚：GPIO6
#define CAMERA_PIN_D4 15 // 摄像头数据位4引脚：GPIO15
#define CAMERA_PIN_D3 17 // 摄像头数据位3引脚：GPIO17
#define CAMERA_PIN_D2 8  // 摄像头数据位2引脚：GPIO8
#define CAMERA_PIN_D1 18 // 摄像头数据位1引脚：GPIO18
#define CAMERA_PIN_D0 16 // 摄像头数据位0引脚：GPIO16

// 摄像头同步信号引脚
#define CAMERA_PIN_VSYNC 3 // 摄像头垂直同步信号引脚：GPIO3
#define CAMERA_PIN_HREF 46 // 摄像头水平参考信号引脚：GPIO46
#define CAMERA_PIN_PCLK 7  // 摄像头像素时钟引脚：GPIO7

#define XCLK_FREQ_HZ 24000000 // 摄像头XCLK时钟频率：24MHz

#endif // _BOARD_CONFIG_H_  // 配置头文件结束