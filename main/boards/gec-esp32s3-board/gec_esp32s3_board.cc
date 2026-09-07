/**
 * @file gec_esp32s3_board.cc
 * @brief 粤嵌ESP32-S3开发板硬件抽象层实现
 * @details
 * 该文件实现了基于ESP32-S3的粤嵌ESP32-S3开发板的硬件抽象层，包括I2C、SPI、显示屏、触摸屏、摄像头等外设的初始化和控制
 */

#include "application.h"            // 应用程序接口
#include "button.h"                 // 按钮处理
#include "codecs/box_audio_codec.h" // 音频编解码器
#include "config.h"                 // 配置文件
#include "display/lcd_display.h"    // LCD显示屏接口
#include "esp32_camera.h"           // ESP32摄像头
#include "i2c_device.h"             // I2C设备基类
#include "wifi_board.h"             // WiFi板基类定义

#include "esp_lcd_st7796.h"       // ST7796 LCD驱动
#include "esp_lcd_touch_ft6x36.h" // FT6X36触摸屏驱动
#include <driver/i2c_master.h>    // I2C主设备驱动
#include <driver/spi_common.h>    // SPI通用驱动
#include <esp_lcd_panel_vendor.h> // LCD面板供应商特定驱动
#include <esp_lcd_touch_ft5x06.h> // FT5x06触摸屏驱动
#include <esp_log.h>              // ESP32日志系统
#include <esp_lvgl_port.h>        // LVGL图形库端口
#include <lvgl.h>                 // LVGL图形库
#include <wifi_station.h>         // WiFi站模式

#define TAG "gec-esp32s3-board" // 日志标签

// 声明LVGL字体资源
LV_FONT_DECLARE(font_puhui_20_4);   // 普惠字体20号4bpp
LV_FONT_DECLARE(font_awesome_20_4); // FontAwesome图标字体20号4bpp

/**
 * @class Pca9557
 * @brief PCA9557 I2C GPIO扩展芯片驱动类
 * @details 继承自I2cDevice，提供对PCA9557芯片的简单控制接口
 */
class Pca9557 : public I2cDevice
{
public:
  /**
   * @brief 构造函数，初始化PCA9557芯片
   * @param i2c_bus I2C总线句柄
   * @param addr 设备地址(0x19)
   */
  Pca9557(i2c_master_bus_handle_t i2c_bus, uint8_t addr)
      : I2cDevice(i2c_bus, addr)
  {
    // 配置输出端口：设置端口0和1为输出模式
    WriteReg(0x01, 0x03); // 配置寄存器：设置P0和P1为输出
    WriteReg(0x03, 0xf8); // 极性反转寄存器：保持默认极性
  }

  /**
   * @brief 设置指定GPIO引脚的输出状态
   * @param bit 引脚位(0-7)
   * @param level 输出电平(0或1)
   */
  void SetOutputState(uint8_t bit, uint8_t level)
  {
    // 读取当前输出状态寄存器(0x01)
    uint8_t data = ReadReg(0x01);
    // 清除对应位，然后设置新值
    data = (data & ~(1 << bit)) | (level << bit);
    // 写回输出状态寄存器
    WriteReg(0x01, data);
  }
};

/**
 * @class CustomAudioCodec
 * @brief 自定义音频编解码器类
 * @details 继承自BoxAudioCodec，添加了PCA9557控制的音频功放使能功能
 */
class CustomAudioCodec : public BoxAudioCodec
{
private:
  Pca9557 *pca9557_; // PCA9557 GPIO扩展芯片指针，用于控制音频功放

public:
  /**
   * @brief 构造函数，初始化音频编解码器
   * @param i2c_bus I2C总线句柄
   * @param pca9557 PCA9557实例指针，用于音频功放控制
   */
  CustomAudioCodec(i2c_master_bus_handle_t i2c_bus, Pca9557 *pca9557)
      : BoxAudioCodec(i2c_bus,
                      AUDIO_INPUT_SAMPLE_RATE,  // 音频输入采样率
                      AUDIO_OUTPUT_SAMPLE_RATE, // 音频输出采样率
                      AUDIO_I2S_GPIO_MCLK,      // I2S主时钟引脚
                      AUDIO_I2S_GPIO_BCLK,      // I2S位时钟引脚
                      AUDIO_I2S_GPIO_WS,        // I2S字选择引脚
                      AUDIO_I2S_GPIO_DOUT,      // I2S数据输出引脚
                      AUDIO_I2S_GPIO_DIN,       // I2S数据输入引脚
                      GPIO_NUM_NC,              // 未使用的引脚
                      AUDIO_CODEC_ES8311_ADDR,  // ES8311 DAC地址
                      AUDIO_CODEC_ES7210_ADDR,  // ES7210 ADC地址
                      AUDIO_INPUT_REFERENCE),   // 音频输入参考电压
        pca9557_(pca9557)
  { // 初始化PCA9557指针
  }

  /**
   * @brief 启用或禁用音频输出
   * @param enable true启用音频输出，false禁用
   * @override 重写基类的EnableOutput方法
   */
  virtual void EnableOutput(bool enable) override
  {
    // 调用基类方法处理音频编解码器本身
    BoxAudioCodec::EnableOutput(enable);

    // 控制PCA9557的P1引脚来控制音频功放
    if (enable)
    {
      pca9557_->SetOutputState(1, 1); // 启用音频功放
    }
    else
    {
      pca9557_->SetOutputState(1, 0); // 禁用音频功放
    }
  }
};

/**
 * @class GecEsp32s3Board
 * @brief 粤嵌ESP32-S3开发板主类
 * @details 继承自WifiBoard，实现粤嵌ESP32-S3开发板的所有硬件功能
 */
class GecEsp32s3Board : public WifiBoard
{
private:
  i2c_master_bus_handle_t i2c_bus_;        // I2C总线句柄
  i2c_master_dev_handle_t pca9557_handle_; // PCA9557设备句柄
  Button boot_button_;                     // 启动按钮实例
  LcdDisplay *display_;                    // LCD显示屏指针
  Pca9557 *pca9557_;                       // PCA9557 GPIO扩展芯片指针
  Esp32Camera *camera_;                    // 摄像头实例指针

  /**
   * @brief 初始化I2C总线和外设
   * @details 配置I2C总线参数并初始化PCA9557 GPIO扩展芯片
   */
  void InitializeI2c()
  {
    // 配置I2C主总线参数
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port = (i2c_port_t)1,             // 使用I2C端口1
        .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN, // SDA引脚
        .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN, // SCL引脚
        .clk_source = I2C_CLK_SRC_DEFAULT,     // 默认时钟源
        .glitch_ignore_cnt = 7,                // 毛刺忽略计数
        .intr_priority = 0,                    // 中断优先级
        .trans_queue_depth = 0,                // 传输队列深度
        .flags =
            {
                .enable_internal_pullup = 1, // 启用内部上拉电阻
            },
    };
    // 创建I2C主总线，检查错误
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));

    // 初始化PCA9557 GPIO扩展芯片，地址0x19
    pca9557_ = new Pca9557(i2c_bus_, 0x19);
  }

  /**
   * @brief 初始化SPI总线
   * @details 配置SPI3总线参数，用于LCD显示屏通信
   */
  void InitializeSpi()
  {
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = GPIO_NUM_40;   // MOSI引脚(GPIO40)
    buscfg.miso_io_num = GPIO_NUM_NC;   // MISO引脚未使用
    buscfg.sclk_io_num = GPIO_NUM_41;   // SCLK引脚(GPIO41)
    buscfg.quadwp_io_num = GPIO_NUM_NC; // Quad SPI写保护未使用
    buscfg.quadhd_io_num = GPIO_NUM_NC; // Quad SPI保持未使用
    // 计算最大传输大小：显示屏宽×高×每个像素2字节(RGB565)
    buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
    // 初始化SPI3总线，使用自动DMA通道
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
  }

  /**
   * @brief 初始化按钮功能
   * @details 配置启动按钮的单击和双击事件处理
   */
  void InitializeButtons()
  {
    // 设置按钮单击事件处理
    boot_button_.OnClick([this]()
                         {
      auto &app = Application::GetInstance(); // 获取应用实例
      // 如果设备正在启动且WiFi未连接，重置WiFi配置
      if (app.GetDeviceState() == kDeviceStateStarting &&
          !WifiStation::GetInstance().IsConnected()) {
        ResetWifiConfiguration();
      }
      // 切换聊天状态
      app.ToggleChatState(); });

// 条件编译：如果启用了设备端AEC功能
#if CONFIG_USE_DEVICE_AEC
    // 设置按钮双击事件处理
    boot_button_.OnDoubleClick([this]()
                               {
      auto &app = Application::GetInstance(); // 获取应用实例
      // 如果设备处于空闲状态，切换AEC模式
      if (app.GetDeviceState() == kDeviceStateIdle) {
        app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide
                                                   : kAecOff);
      } });
#endif
  }

  /**
   * @brief 初始化ST7789显示屏
   * @details 配置ST7789驱动芯片的SPI接口和显示参数
   */
  void InitializeSt7789Display()
  {
    esp_lcd_panel_io_handle_t panel_io = nullptr; // LCD面板IO句柄
    esp_lcd_panel_handle_t panel = nullptr;       // LCD面板句柄

    // 液晶屏控制IO初始化
    ESP_LOGD(TAG, "Install panel IO"); // 调试日志
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = GPIO_NUM_NC;  // 片选引脚未使用
    io_config.dc_gpio_num = GPIO_NUM_39;  // 数据/命令选择引脚(GPIO39)
    io_config.spi_mode = 2;               // SPI模式2(CPOL=1, CPHA=0)
    io_config.pclk_hz = 80 * 1000 * 1000; // 像素时钟80MHz
    io_config.trans_queue_depth = 10;     // 传输队列深度
    io_config.lcd_cmd_bits = 8;           // 命令位宽8位
    io_config.lcd_param_bits = 8;         // 参数位宽8位
    // 创建SPI面板IO接口
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

    // 初始化液晶屏驱动芯片ST7789
    ESP_LOGD(TAG, "Install LCD driver");
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = GPIO_NUM_NC;              // 复位引脚未使用
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB; // RGB颜色顺序
    panel_config.bits_per_pixel = 16;                       // 每像素16位(RGB565)
    // 创建ST7789面板驱动
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

    // 复位面板并控制背光
    esp_lcd_panel_reset(panel);
    pca9557_->SetOutputState(0, 0); // 控制PCA9557的P0引脚(背光)

    // 初始化面板配置
    esp_lcd_panel_init(panel);                                       // 初始化面板
    esp_lcd_panel_invert_color(panel, true);                         // 颜色反转
    esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);                   // 交换XY坐标
    esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y); // 镜像设置

    // 创建SPI LCD显示实例
    display_ = new SpiLcdDisplay(
        panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X,
        DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
        {
            .text_font = &font_puhui_20_4,   // 文本字体
            .icon_font = &font_awesome_20_4, // 图标字体
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
            .emoji_font = font_emoji_32_init(), // 微信风格表情字体32px
#else
            .emoji_font = font_emoji_64_init(), // 默认表情字体64px
#endif
        });
  }

  /**
   * @brief 初始化ST7796显示屏
   * @details 配置ST7796驱动芯片的SPI接口和显示参数
   */
  void InitializeSt7796Display()
  {
    esp_lcd_panel_io_handle_t panel_io = nullptr; // LCD面板IO句柄
    esp_lcd_panel_handle_t panel = nullptr;       // LCD面板句柄

    // 液晶屏控制IO初始化
    ESP_LOGD(TAG, "Install panel IO");
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = GPIO_NUM_NC;  // 片选引脚未使用
    io_config.dc_gpio_num = GPIO_NUM_39;  // 数据/命令选择引脚(GPIO39)
    io_config.spi_mode = 0;               // SPI模式0(CPOL=0, CPHA=0)
    io_config.pclk_hz = 80 * 1000 * 1000; // 像素时钟80MHz
    io_config.trans_queue_depth = 10;     // 传输队列深度
    io_config.lcd_cmd_bits = 8;           // 命令位宽8位
    io_config.lcd_param_bits = 8;         // 参数位宽8位
    // 创建SPI面板IO接口
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

    // 初始化液晶屏驱动芯片ST7796
    ESP_LOGI(TAG, "Install LCD driver With ST7796"); // 信息日志
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = GPIO_NUM_NC;              // 复位引脚未使用
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR; // BGR颜色顺序
    panel_config.bits_per_pixel = 16;                       // 每像素16位(RGB565)
    // 创建ST7796面板驱动
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(panel_io, &panel_config, &panel));

    // 复位面板并控制背光
    esp_lcd_panel_reset(panel);
    pca9557_->SetOutputState(0, 0); // 控制PCA9557的P0引脚(背光)

    // 初始化面板配置
    esp_lcd_panel_init(panel);                                       // 初始化面板
    esp_lcd_panel_invert_color(panel, true);                         // 颜色反转
    esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);                   // 交换XY坐标
    esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y); // 镜像设置

    // 创建SPI LCD显示实例
    display_ = new SpiLcdDisplay(
        panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X,
        DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
        {
            .text_font = &font_puhui_20_4,   // 文本字体
            .icon_font = &font_awesome_20_4, // 图标字体
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
            .emoji_font = font_emoji_32_init(), // 微信风格表情字体32px
#else
            .emoji_font = font_emoji_64_init(), // 默认表情字体64px
#endif
        });
  }

  /**
   * @brief 初始化触摸屏功能
   * @details 配置触摸屏驱动和LVGL触摸接口
   */
  void InitializeTouch()
  {
    esp_lcd_touch_handle_t tp; // 触摸屏句柄
    // 配置触摸屏参数
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = DISPLAY_HEIGHT,     // X轴最大值(与显示屏高度对应)
        .y_max = DISPLAY_WIDTH,      // Y轴最大值(与显示屏宽度对应)
        .rst_gpio_num = GPIO_NUM_NC, // 复位引脚与LCD共享
        .int_gpio_num = GPIO_NUM_NC, // 中断引脚未使用
        .levels =
            {
                .reset = 0,     // 复位电平
                .interrupt = 0, // 中断电平
            },
        .flags =
            {
                .swap_xy = 1,  // 交换XY坐标
                .mirror_x = 1, // X轴镜像
                .mirror_y = 0, // Y轴不镜像
            },
    };
    esp_lcd_panel_io_handle_t tp_io_handle = NULL; // 触摸屏IO句柄
#ifdef TP_DRIVER_FT5X06    
    // 配置I2C触摸屏IO参数
    esp_lcd_panel_io_i2c_config_t tp_io_config =
        ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    tp_io_config.scl_speed_hz = 400000; // I2C时钟频率400kHz

    // 创建I2C触摸屏IO接口
    esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle);

    // 创建FT5x06触摸屏驱动实例
    esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp);
#elifdef TP_DRIVER_FT6X36
    // 配置I2C触摸屏IO参数
    esp_lcd_panel_io_i2c_config_t tp_io_config =
        ESP_LCD_TOUCH_IO_I2C_FT6x36_CONFIG();
    tp_io_config.scl_speed_hz = 400000; // I2C时钟频率400kHz

    // 创建I2C触摸屏IO接口
    esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle);
    // 创建FT6X36触摸屏驱动实例
    esp_lcd_touch_new_i2c_ft6x36(tp_io_handle, &tp_cfg, &tp);
#endif
    assert(tp); // 确保触摸屏初始化成功

    /* 添加触摸输入到LVGL(针对选定的屏幕) */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lv_display_get_default(), // 默认显示设备
        .handle = tp,                     // 触摸屏句柄
    };

    // 将触摸屏添加到LVGL端口
    lvgl_port_add_touch(&touch_cfg);
  }

  /**
   * @brief 初始化摄像头
   * @details 配置OV2640摄像头模块并初始化ESP32摄像头驱动
   */
  void InitializeCamera()
  {
    // 打开摄像头电源(通过PCA9557的P2引脚)
    pca9557_->SetOutputState(2, 0);

    // 配置摄像头参数
    camera_config_t config = {};
    config.ledc_channel =LEDC_CHANNEL_2; // LEDC通道选择(用于生成XCLK时钟，但S3不用)
    config.ledc_timer =LEDC_TIMER_2;                          // LEDC定时器选择(用于生成XCLK时钟，但S3不用)
    config.pin_d0 = CAMERA_PIN_D0;             // 数据引脚D0
    config.pin_d1 = CAMERA_PIN_D1;             // 数据引脚D1
    config.pin_d2 = CAMERA_PIN_D2;             // 数据引脚D2
    config.pin_d3 = CAMERA_PIN_D3;             // 数据引脚D3
    config.pin_d4 = CAMERA_PIN_D4;             // 数据引脚D4
    config.pin_d5 = CAMERA_PIN_D5;             // 数据引脚D5
    config.pin_d6 = CAMERA_PIN_D6;             // 数据引脚D6
    config.pin_d7 = CAMERA_PIN_D7;             // 数据引脚D7
    config.pin_xclk = CAMERA_PIN_XCLK;         // XCLK时钟引脚
    config.pin_pclk = CAMERA_PIN_PCLK;         // PCLK像素时钟引脚
    config.pin_vsync = CAMERA_PIN_VSYNC;       // VSYNC垂直同步引脚
    config.pin_href = CAMERA_PIN_HREF;         // HREF水平参考引脚
    config.pin_sccb_sda = -1;                  // SCCB SDA引脚(-1表示使用已初始化的I2C接口)
    config.pin_sccb_scl = CAMERA_PIN_SIOC;     // SCCB SCL引脚
    config.sccb_i2c_port = 1;                  // SCCB I2C端口号
    config.pin_pwdn = CAMERA_PIN_PWDN;         // 电源关闭引脚
    config.pin_reset = CAMERA_PIN_RESET;       // 复位引脚
    config.xclk_freq_hz = XCLK_FREQ_HZ;        // XCLK时钟频率
    config.pixel_format = PIXFORMAT_RGB565;    // 像素格式RGB565
    config.frame_size = FRAMESIZE_VGA;         // 帧尺寸VGA(640x480)
    config.jpeg_quality = 12;                  // JPEG质量(0-63，越小质量越好)
    config.fb_count = 1;                       // 帧缓冲区数量
    config.fb_location = CAMERA_FB_IN_PSRAM;   // 帧缓冲区位置(PSRAM)
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY; // 抓取模式(缓冲区空时抓取)

    // 创建摄像头实例
    camera_ = new Esp32Camera(config);
  }

public:
  /**
   * @brief 构造函数，初始化整个开发板
   * @details 按顺序初始化所有硬件组件
   */
  GecEsp32s3Board() : boot_button_(BOOT_BUTTON_GPIO)
  {                  // 初始化启动按钮
    InitializeI2c(); // 初始化I2C总线
    InitializeSpi(); // 初始化SPI总线

    // 条件编译：根据LCD驱动芯片类型选择初始化函数
#if defined(LCD_DRIVER_ST7789V2)
    InitializeSt7789Display(); // 初始化ST7789显示屏
#elif defined(LCD_DRIVER_ST7796)
    InitializeSt7796Display(); // 初始化ST7796显示屏
#endif

    //InitializeTouch();   // 初始化触摸屏
    InitializeButtons(); // 初始化按钮功能
    InitializeCamera();  // 初始化摄像头

    // 恢复背光亮度设置
    GetBacklight()->RestoreBrightness();
  }

  /**
   * @brief 获取音频编解码器实例
   * @return AudioCodec* 音频编解码器指针
   * @override 重写基类的GetAudioCodec方法
   */
  virtual AudioCodec *GetAudioCodec() override
  {
    // 使用静态变量确保单例模式
    static CustomAudioCodec audio_codec(i2c_bus_,  // I2C总线句柄
                                        pca9557_); // PCA9557实例
    return &audio_codec;
  }

  /**
   * @brief 获取显示屏实例
   * @return Display* 显示屏指针
   * @override 重写基类的GetDisplay方法
   */
  virtual Display *GetDisplay() override { return display_; }

  /**
   * @brief 获取背光控制器实例
   * @return Backlight* 背光控制器指针
   * @override 重写基类的GetBacklight方法
   */
  virtual Backlight *GetBacklight() override
  {
    // 使用静态变量确保单例模式
    static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, false);
    return &backlight;
  }

  /**
   * @brief 获取摄像头实例
   * @return Camera* 摄像头指针
   * @override 重写基类的GetCamera方法
   */
  virtual Camera *GetCamera() override { return camera_; }
};

// 声明开发板实例，供系统识别和使用
DECLARE_BOARD(GecEsp32s3Board);