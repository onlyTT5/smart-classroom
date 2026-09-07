/**
 * @file mcp_server.cc
 * @brief MCP (Model Context Protocol) 服务器实现文件
 *
 * @details
 * 本文件实现了MCP服务器的所有功能，包括工具管理、协议解析和工具执行。
 * MCP协议允许AI模型通过标准化的JSON-RPC接口调用设备上的各种功能。
 *
 * 主要功能模块：
 * - 工具注册和管理（AddTool系列方法）
 * - 常用工具集（AddCommonTools）
 * - 协议消息解析（ParseMessage）
 * - 工具调用执行（DoToolCall）
 * - 响应生成（ReplyResult/ReplyError）
 *
 * 内置工具包括：
 * - self.get_device_status: 获取设备状态
 * - self.audio_speaker.set_volume: 设置音量
 * - self.screen.set_brightness: 设置屏幕亮度
 * - self.screen.set_theme: 设置主题
 * - self.camera.take_photo: 拍照
 * - self.led.turn_on/off: LED控制
 * - self.dht11.get_temperature/humidity: DHT11传感器
 * - self.myinfo.play_info: 播放信息
 *
 * 使用示例：
 * @code
 * // 初始化MCP服务器
 * auto& mcp = McpServer::GetInstance();
 * mcp.AddCommonTools();
 *
 * // 处理MCP消息
 * std::string message = R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})";
 * mcp.ParseMessage(message);
 * @endcode
 *
 * @see https://modelcontextprotocol.io/specification/2024-11-05
 */

#include "mcp_server.h"
#include <esp_log.h>
#include <esp_app_desc.h>
#include <algorithm>
#include <cstring>
#include <esp_pthread.h>

#include "application.h"
#include "display.h"
#include "board.h"

#define TAG "MCP" ///< ESP日志标签

/**
 * @brief 构造函数
 * @details 初始化MCP服务器实例，工具列表初始为空
 */
McpServer::McpServer()
{
}

/**
 * @brief 析构函数
 * @details 释放所有工具对象并清空工具列表
 */
McpServer::~McpServer()
{
    for (auto tool : tools_)
    {
        delete tool;
    }
    tools_.clear();
}

/**
 * @brief 添加常用工具集
 * @details 注册一组预定义的设备控制工具到MCP服务器。
 *
 * 实现策略：
 * 1. 备份现有工具列表
 * 2. 将常用工具添加到列表开头（利用prompt cache优化）
 * 3. 恢复原始工具列表到末尾
 *
 * 注册的工具包括：
 * - self.get_device_status: 获取设备综合状态
 * - self.audio_speaker.set_volume: 设置音频音量(0-100)
 * - self.screen.set_brightness: 设置屏幕亮度(0-100)
 * - self.screen.set_theme: 设置显示主题(light/dark)
 * - self.camera.take_photo: 拍照并分析
 * - self.led.turn_on/off: GPIO10 LED控制
 * - self.dht11.get_temperature/humidity: 温湿度传感器
 * - self.myinfo.play_info/play_all_info: 信息播报
 *
 * @note 自定义工具不应在此添加，应在Board::InitializeTools中添加
 */
void McpServer::AddCommonTools()
{
    // *Important* To speed up the response time, we add the common tools to the beginning of
    // the tools list to utilize the prompt cache.
    // **重要** 为了提升响应速度，我们把常用的工具放在前面，利用 prompt cache 的特性。

    // Backup the original tools list and restore it after adding the common tools.
    // 备份原始工具列表，在添加常用工具后恢复
    auto original_tools = std::move(tools_); // 使用移动语义将原始工具列表移动到临时变量，避免拷贝
    auto &board = Board::GetInstance();      // 获取Board单例实例的引用，用于访问硬件相关功能

    // Do not add custom tools here.
    // Custom tools must be added in the board's InitializeTools function.
    // 不要在这里添加自定义工具
    // 自定义工具必须在board的InitializeTools函数中添加

    // Add tool for getting device status

    // 添加获取设备状态的工具
    AddTool("self.get_device_status",                                                                                                               // 工具名称：获取设备状态
            "Provides the real-time information of the device, including the current status of the audio speaker, screen, battery, network, etc.\n" // 工具描述：提供设备的实时信息，包括音频扬声器、屏幕、电池、网络等当前状态
            "Use this tool for: \n"                                                                                                                 // 使用场景说明
            "1. Answering questions about current condition (e.g. what is the current volume of the audio speaker?)\n"                              // 场景1：回答关于当前状态的问题（例如：音频扬声器的当前音量是多少？）
            "2. As the first step to control the device (e.g. turn up / down the volume of the audio speaker, etc.)",                               // 场景2：作为控制设备的第一步（例如：调高/调低音频扬声器的音量等）
            PropertyList(),                                                                                                                         // 工具参数列表：无参数
            [&board](const PropertyList &properties) -> ReturnValue {                                                                               // Lambda表达式：工具回调函数，捕获board引用
                return board.GetDeviceStatusJson();                                                                                                 // 调用board的GetDeviceStatusJson方法获取设备状态JSON
            });                                                                                                                                     // Lambda表达式结束

    // Add tool for setting audio speaker volume
    // 添加设置音频扬声器音量的工具
    AddTool("self.audio_speaker.set_volume",                                                                                                                     // 工具名称：设置音频扬声器音量
            "Set the volume of the audio speaker. If the current volume is unknown, you must call `self.get_device_status` tool first and then call this tool.", // 工具描述：设置音频扬声器的音量。如果当前音量未知，必须先调用`self.get_device_status`工具，然后再调用此工具
            PropertyList({
                // 工具参数列表：包含一个整数类型参数
                Property("volume", kPropertyTypeInteger, 0, 100)           // 参数名称：volume，类型：整数，范围：0-100
            }),                                                            // 参数列表结束
            [&board](const PropertyList &properties) -> ReturnValue {      // Lambda表达式：工具回调函数，捕获board引用
                auto codec = board.GetAudioCodec();                        // 获取音频编解码器实例
                codec->SetOutputVolume(properties["volume"].value<int>()); // 设置输出音量，从参数列表中获取volume值并转换为整数
                return true;                                               // 返回成功标志
            });                                                            // Lambda表达式结束

    // Get backlight instance from board
    // 从board获取背光实例
    auto backlight = board.GetBacklight(); // 获取背光控制实例
    if (backlight)
    { // 检查背光实例是否存在
        // Add tool for setting screen brightness if backlight is available
        // 如果背光可用，添加设置屏幕亮度的工具
        AddTool("self.screen.set_brightness",        // 工具名称：设置屏幕亮度
                "Set the brightness of the screen.", // 工具描述：设置屏幕的亮度
                PropertyList({
                    // 工具参数列表：包含一个整数类型参数
                    Property("brightness", kPropertyTypeInteger, 0, 100)                              // 参数名称：brightness，类型：整数，范围：0-100
                }),                                                                                   // 参数列表结束
                [backlight](const PropertyList &properties) -> ReturnValue {                          // Lambda表达式：工具回调函数，捕获backlight引用
                    uint8_t brightness = static_cast<uint8_t>(properties["brightness"].value<int>()); // 从参数列表获取brightness值并转换为uint8_t类型
                    backlight->SetBrightness(brightness, true);                                       // 设置背光亮度，第二个参数true表示立即应用
                    return true;                                                                      // 返回成功标志
                });                                                                                   // Lambda表达式结束
    } // if语句结束

    // Get display instance from board
    // 从board获取显示实例
    auto display = board.GetDisplay(); // 获取显示实例
    if (display && !display->GetTheme().empty())
    { // 检查显示实例是否存在且主题不为空
        // Add tool for setting screen theme if display supports themes
        // 如果显示支持主题，添加设置屏幕主题的工具
        AddTool("self.screen.set_theme",                                            // 工具名称：设置屏幕主题
                "Set the theme of the screen. The theme can be `light` or `dark`.", // 工具描述：设置屏幕的主题。主题可以是`light`（浅色）或`dark`（深色）
                PropertyList({
                    // 工具参数列表：包含一个字符串类型参数
                    Property("theme", kPropertyTypeString)                               // 参数名称：theme，类型：字符串
                }),                                                                      // 参数列表结束
                [display](const PropertyList &properties) -> ReturnValue {               // Lambda表达式：工具回调函数，捕获display引用
                    display->SetTheme(properties["theme"].value<std::string>().c_str()); // 设置显示主题，从参数列表获取theme值并转换为C风格字符串
                    return true;                                                         // 返回成功标志
                });                                                                      // Lambda表达式结束
    } // if语句结束

    // Get camera instance from board
    // 从board获取摄像头实例
    auto camera = board.GetCamera(); // 获取摄像头实例
    if (camera)
    { // 检查摄像头实例是否存在
        // Add tool for taking photo and explaining it if camera is available
        // 如果摄像头可用，添加拍照并解释的工具
        AddTool("self.camera.take_photo",                                                                // 工具名称：拍照并解释
                "Take a photo and explain it. Use this tool after the user asks you to see something.\n" // 工具描述：拍照并解释照片内容。当用户要求你查看某物时使用此工具
                "Args:\n"                                                                                // 参数说明
                "  `question`: The question that you want to ask about the photo.\n"                     // 参数question：你想问关于照片的问题
                "Return:\n"                                                                              // 返回值说明
                "  A JSON object that provides the photo information.",                                  // 返回：提供照片信息的JSON对象
                PropertyList({
                    // 工具参数列表：包含一个字符串类型参数
                    Property("question", kPropertyTypeString)             // 参数名称：question，类型：字符串
                }),                                                       // 参数列表结束
                [camera](const PropertyList &properties) -> ReturnValue { // Lambda表达式：工具回调函数，捕获camera引用
                    // Lower the priority to do the camera capture
                    // 降低任务优先级以执行摄像头捕获
                    TaskPriorityReset priority_reset(1); // 创建任务优先级重置对象，优先级设为1

                    if (!camera->Capture())
                    {                                                                            // 尝试捕获照片
                        return "{\"success\": false, \"message\": \"Failed to capture photo\"}"; // 捕获失败，返回错误信息
                    } // if语句结束
                    auto question = properties["question"].value<std::string>(); // 从参数列表获取question值
                    return camera->Explain(question);                            // 调用摄像头解释方法，传入问题并返回解释结果
                });                                                              // Lambda表达式结束
    } // if语句结束

    // Add LED control tools
    // 添加LED控制工具
    // Add tool for turning on LED
    // 添加打开LED的工具
    AddTool("self.led.led_on",                                  // 工具名称：打开LED
            "Turn on the LED light connected to GPIO10.",       // 工具描述：打开连接到GPIO10的LED灯
            PropertyList(),                                     // 工具参数列表：无参数
            [](const PropertyList &properties) -> ReturnValue { // Lambda表达式：工具回调函数
                auto &app = Application::GetInstance();         // 获取Application单例实例的引用
                esp_err_t ret = app.GetLedCtrl().led_on();      // 调用LED控制器的led_on方法打开LED
                if (ret == ESP_OK)
                {                                                                 // 检查操作是否成功
                    return "{\"success\": true, \"message\": \"LED turned on\"}"; // 成功：返回成功信息和消息
                }
                else
                {                                                                          // 操作失败
                    return "{\"success\": false, \"message\": \"Failed to turn on LED\"}"; // 失败：返回失败信息和错误消息
                } // if-else语句结束
            }); // Lambda表达式结束

    // Add tool for turning off LED
    // 添加关闭LED的工具
    AddTool("self.led.led_off",                                 // 工具名称：关闭LED
            "Turn off the LED light connected to GPIO10.",      // 工具描述：关闭连接到GPIO10的LED灯
            PropertyList(),                                     // 工具参数列表：无参数
            [](const PropertyList &properties) -> ReturnValue { // Lambda表达式：工具回调函数
                auto &app = Application::GetInstance();         // 获取Application单例实例的引用
                esp_err_t ret = app.GetLedCtrl().led_off();     // 调用LED控制器的led_off方法关闭LED
                if (ret == ESP_OK)
                {                                                                  // 检查操作是否成功
                    return "{\"success\": true, \"message\": \"LED turned off\"}"; // 成功：返回成功信息和消息
                }
                else
                {                                                                           // 操作失败
                    return "{\"success\": false, \"message\": \"Failed to turn off LED\"}"; // 失败：返回失败信息和错误消息
                } // if-else语句结束
            }); // Lambda表达式结束

    // Add tool for getting LED state
    // 添加获取LED状态的工具
    AddTool("self.led.get_state",                                // 工具名称：获取LED状态
            "Get current state of the LED light.",               // 工具描述：获取LED灯的当前状态
            PropertyList(),                                      // 工具参数列表：无参数
            [](const PropertyList &properties) -> ReturnValue {  // Lambda表达式：工具回调函数
                auto &app = Application::GetInstance();          // 获取Application单例实例的引用
                uint32_t sta = app.GetLedCtrl().led_get_state(); // 调用LED控制器的GetState方法获取LED状态

                if (sta)
                {
                    return "{\"state\": true}";
                }
                else
                {
                    return "{\"state\": false}";
                }

            }); // Lambda表达式结束

    // 添加翻转LED状态的工具
    AddTool("self.led.led_toggle", // 工具名称：翻转LED
            "Toggle the LED light state. If the LED is currently on, turn it off; if it is off, turn it on. "
            "当用户说“灯状态翻转”“翻转灯”“切换灯”等指令时调用此工具。", // 工具描述（包含触发指令关键词）
            PropertyList(),                                             // 无参数
            [](const PropertyList &properties) -> ReturnValue {         // 工具回调
                auto &app = Application::GetInstance();
                esp_err_t ret = app.GetLedCtrl().led_toggle(); // 调用翻转方法
                if (ret == ESP_OK)
                {
                    return "{\"success\": true, \"message\": \"LED toggled\"}";
                }
                else
                {
                    return "{\"success\": false, \"message\": \"Failed to toggle LED\"}";
                }
            });

    // Add DHT11 sensor tools
    // 添加DHT11传感器工具
    // Add tool for getting temperature from DHT11 sensor
    // 添加从DHT11传感器获取温度的工具
    AddTool("self.dht11.get_temperature",                                                         // 工具名称：获取DHT11温度
            "Get the current temperature from DHT11 sensor.",                                     // 工具描述：从DHT11传感器获取当前温度
            PropertyList(),                                                                       // 工具参数列表：无参数
            [](const PropertyList &properties) -> ReturnValue {                                   // Lambda表达式：工具回调函数
                auto &app = Application::GetInstance();                                           // 获取Application单例实例的引用
                float temperature = app.GetDht11Sensor().GetTemperature();                        // 调用DHT11传感器的GetTemperature方法获取温度值
                return "{\"temperature\": " + std::to_string(temperature) + ", \"unit\": \"℃\"}"; // 返回包含温度值和单位的JSON字符串
            });                                                                                   // Lambda表达式结束

    // Add tool for getting humidity from DHT11 sensor
    // 添加从DHT11传感器获取湿度的工具
    AddTool("self.dht11.get_humidity",                                                      // 工具名称：获取DHT11湿度
            "Get the current humidity from DHT11 sensor.",                                  // 工具描述：从DHT11传感器获取当前湿度
            PropertyList(),                                                                 // 工具参数列表：无参数
            [](const PropertyList &properties) -> ReturnValue {                             // Lambda表达式：工具回调函数
                auto &app = Application::GetInstance();                                     // 获取Application单例实例的引用
                float humidity = app.GetDht11Sensor().GetHumidity();                        // 调用DHT11传感器的GetHumidity方法获取湿度值
                return "{\"humidity\": " + std::to_string(humidity) + ", \"unit\": \"%\"}"; // 返回包含湿度值和单位的JSON字符串
            });                                                                             // Lambda表达式结束

    // Add tool for getting temperature and humidity from DHT11 sensor
    // 添加从DHT11传感器获取温度和湿度的工具
    AddTool("self.dht11.get_temperature_and_humidity",                 // 工具名称：获取DHT11温度和湿度
            "Get current temperature and humidity from DHT11 sensor.", // 工具描述：从DHT11传感器获取当前温度和湿度
            PropertyList(),                                            // 工具参数列表：无参数
            [](const PropertyList &properties) -> ReturnValue {        // Lambda表达式：工具回调函数
                auto &app = Application::GetInstance();                // 获取Application单例实例的引用
                float temperature, humidity;                           // 声明温度和湿度变量
                if (app.GetDht11Sensor().GetTemperatureAndHumidity(temperature, humidity))
                {                                                                                                                                                                           // 调用DHT11传感器的GetTemperatureAndHumidity方法获取温度和湿度值
                    return "{\"temperature\": " + std::to_string(temperature) + ", \"humidity\": " + std::to_string(humidity) + ", \"temperature_unit\": \"℃\", \"humidity_unit\": \"%\"}"; // 成功：返回包含温度和湿度值及单位的JSON字符串
                }
                else
                {                                                                                // 获取失败
                    return "{\"success\": false, \"message\": \"Failed to read DHT11 sensor\"}"; // 失败：返回失败信息和错误消息
                } // if-else语句结束
            }); // Lambda表达式结束

    // Add MyInfo tools
    // 添加MyInfo工具
    // Add tool for playing specific information by index
    // 添加按索引播放特定信息的工具
    AddTool("self.myinfo.play_info",                            // 工具名称：播放信息
            "Play specific information by index (1, 2, or 3).", // 工具描述：按索引（1、2或3）播放特定信息
            PropertyList({
                // 工具参数列表：包含一个整数类型参数
                Property("index", kPropertyTypeInteger)         // 参数名称：index，类型：整数
            }),                                                 // 参数列表结束
            [](const PropertyList &properties) -> ReturnValue { // Lambda表达式：工具回调函数
                auto &app = Application::GetInstance();         // 获取Application单例实例的引用
                int index = properties["index"].value<int>();   // 从参数列表获取index值并转换为整数
                auto error = app.GetMyInfo().PlayInfo(index);   // 调用MyInfo的PlayInfo方法播放指定索引的信息
                if (error == MyInfoError::SUCCESS)
                {                                                                                   // 检查操作是否成功
                    return "{\"success\": true, \"message\": \"Information played successfully\"}"; // 成功：返回成功信息和消息
                }
                else
                {                                                                               // 操作失败
                    return "{\"success\": false, \"message\": \"Failed to play information\"}"; // 失败：返回失败信息和错误消息
                } // if-else语句结束
            }); // Lambda表达式结束

    // Add tool for playing all information in sequence
    // 添加按顺序播放所有信息的工具
    AddTool("self.myinfo.play_all_info",                        // 工具名称：播放所有信息
            "Play all information in sequence.",                // 工具描述：按顺序播放所有信息
            PropertyList(),                                     // 工具参数列表：无参数
            [](const PropertyList &properties) -> ReturnValue { // Lambda表达式：工具回调函数
                auto &app = Application::GetInstance();         // 获取Application单例实例的引用
                auto error = app.GetMyInfo().PlayAllInfo();     // 调用MyInfo的PlayAllInfo方法播放所有信息
                if (error == MyInfoError::SUCCESS)
                {                                                                                       // 检查操作是否成功
                    return "{\"success\": true, \"message\": \"All information played successfully\"}"; // 成功：返回成功信息和消息
                }
                else
                {                                                                                   // 操作失败
                    return "{\"success\": false, \"message\": \"Failed to play all information\"}"; // 失败：返回失败信息和错误消息
                } // if-else语句结束
            }); // Lambda表达式结束

    // Add tool for getting specific information by index
    // 添加按索引获取特定信息的工具
    AddTool("self.myinfo.get_info",                            // 工具名称：获取信息
            "Get specific information by index (1, 2, or 3).", // 工具描述：按索引（1、2或3）获取特定信息
            PropertyList({
                // 工具参数列表：包含一个整数类型参数
                Property("index", kPropertyTypeInteger)            // 参数名称：index，类型：整数
            }),                                                    // 参数列表结束
            [](const PropertyList &properties) -> ReturnValue {    // Lambda表达式：工具回调函数
                auto &app = Application::GetInstance();            // 获取Application单例实例的引用
                int index = properties["index"].value<int>();      // 从参数列表获取index值并转换为整数
                InfoEntry info;                                    // 声明信息条目变量
                auto error = app.GetMyInfo().GetInfo(index, info); // 调用MyInfo的GetInfo方法获取指定索引的信息
                if (error == MyInfoError::SUCCESS)
                {                                                                                                           // 检查操作是否成功
                    return "{\"success\": true, \"title\": \"" + info.title + "\", \"content\": \"" + info.content + "\"}"; // 成功：返回包含信息标题和内容的JSON字符串
                }
                else
                {                                                                              // 获取失败
                    return "{\"success\": false, \"message\": \"Failed to get information\"}"; // 失败：返回失败信息和错误消息
                } // if-else语句结束
            }); // Lambda表达式结束

    // Add tool for getting total number of information entries
    // 添加获取信息条目总数的工具
    AddTool("self.myinfo.get_info_count",                                               // 工具名称：获取信息数量
            "Get the total number of information entries.",                             // 工具描述：获取信息条目的总数
            PropertyList(),                                                             // 工具参数列表：无参数
            [](const PropertyList &properties) -> ReturnValue {                         // Lambda表达式：工具回调函数
                auto &app = Application::GetInstance();                                 // 获取Application单例实例的引用
                int count = app.GetMyInfo().GetInfoCount();                             // 调用MyInfo的GetInfoCount方法获取信息条目数量
                return "{\"success\": true, \"count\": " + std::to_string(count) + "}"; // 返回包含信息数量的JSON字符串
            });                                                                         // Lambda表达式结束

    // Add tool for displaying activation code
    // 添加显示激活码的工具
    AddTool("self.device.show_activation_code",                                              // 工具名称：显示激活码
            "Display a 6-digit activation code on the device screen and play it via audio. " // 工具描述：在设备屏幕上显示6位激活码并通过音频播报
            "Use this tool when the user wants to bind the device to their account.",        // 使用场景：当用户想要将设备绑定到他们的账户时使用此工具
            PropertyList({
                // 工具参数列表：包含一个字符串类型参数
                Property("code", kPropertyTypeString)                // 参数名称：code，类型：字符串，6位数字激活码
            }),                                                      // 参数列表结束
            [](const PropertyList &properties) -> ReturnValue {      // Lambda表达式：工具回调函数
                auto &app = Application::GetInstance();              // 获取Application单例实例的引用
                auto code = properties["code"].value<std::string>(); // 从参数列表获取code值
                // 验证激活码是否为6位数字
                if (code.length() != 6)
                {                                                                                     // 检查激活码长度是否为6位
                    return "{\"success\": false, \"message\": \"Activation code must be 6 digits\"}"; // 长度不为6位，返回错误信息
                } // if语句结束
                for (char c : code)
                { // 遍历激活码中的每个字符
                    if (!isdigit(c))
                    {                                                                                             // 检查字符是否为数字
                        return "{\"success\": false, \"message\": \"Activation code must contain only digits\"}"; // 包含非数字字符，返回错误信息
                    } // if语句结束
                } // for循环结束
                // 调用Application的ShowActivationCode方法显示激活码
                app.ShowActivationCode(code, "激活码是" + code);                                        // 显示激活码并播报
                return "{\"success\": true, \"message\": \"Activation code displayed: " + code + "\"}"; // 返回成功信息和激活码
            });                                                                                         // Lambda表达式结束

    // Restore the original tools list to the end of the tools list
    // 将原始工具列表恢复到工具列表的末尾
    tools_.insert(tools_.end(), original_tools.begin(), original_tools.end()); // 将备份的原始工具列表插入到当前工具列表的末尾
} // 函数结束

/**
 * @brief 添加工具（通过指针）
 * @param tool 工具对象指针
 * @details 将工具添加到服务器，如果同名工具已存在则忽略并记录警告日志。
 * 工具对象的生命周期由McpServer管理，会在析构时自动释放。
 *
 * 重复检查逻辑：
 * - 遍历现有工具列表
 * - 比较工具名称
 * - 发现重复则记录警告并返回
 */
void McpServer::AddTool(McpTool *tool)
{
    // Prevent adding duplicate tools
    if (std::find_if(tools_.begin(), tools_.end(), [tool](const McpTool *t)
                     { return t->name() == tool->name(); }) != tools_.end())
    {
        ESP_LOGW(TAG, "Tool %s already added", tool->name().c_str());
        return;
    }

    ESP_LOGI(TAG, "Add tool: %s", tool->name().c_str());
    tools_.push_back(tool);
}

/**
 * @brief 添加工具（通过参数）
 * @param name 工具名称
 * @param description 工具描述
 * @param properties 工具参数列表
 * @param callback 工具执行回调函数
 * @details 便捷方法，自动创建McpTool对象并调用AddTool(McpTool*)方法。
 * 适用于简单场景，无需手动管理McpTool对象的生命周期。
 *
 * 示例：
 * @code
 * mcp_server.AddTool("device.reboot", "Reboot the device",
 *     PropertyList(),
 *     [](const PropertyList& props) -> ReturnValue {
 *         esp_restart();
 *         return true;
 *     });
 * @endcode
 */
void McpServer::AddTool(const std::string &name, const std::string &description, const PropertyList &properties, std::function<ReturnValue(const PropertyList &)> callback)
{
    AddTool(new McpTool(name, description, properties, callback));
}

/**
 * @brief 添加仅用户可见的工具
 * @param name 工具名称
 * @param description 工具描述
 * @param properties 工具参数列表
 * @param callback 工具执行回调函数
 * @details 创建的工具对AI不可见，仅用于用户直接调用。
 * 通过设置user_only标志，该工具不会出现在AI的工具列表中。
 *
 * 使用场景：
 * - 用户特定的管理功能
 * - 调试和诊断工具
 * - 不应由AI自动触发的敏感操作
 */
void McpServer::AddUserOnlyTool(const std::string &name, const std::string &description, const PropertyList &properties, std::function<ReturnValue(const PropertyList &)> callback)
{
    auto tool = new McpTool(name, description, properties, callback);
    tool->set_user_only(true);
    AddTool(tool);
}

/**
 * @brief 解析MCP协议消息（字符串版本）
 * @param message JSON格式的消息字符串
 * @details 将JSON字符串解析为cJSON对象，然后调用ParseMessage(cJSON*)处理。
 * 如果解析失败，会记录错误日志。
 *
 * 处理流程：
 * 1. 使用cJSON_Parse解析字符串
 * 2. 调用ParseMessage(cJSON*)处理
 * 3. 释放cJSON对象
 */
void McpServer::ParseMessage(const std::string &message)
{
    cJSON *json = cJSON_Parse(message.c_str());
    if (json == nullptr)
    {
        ESP_LOGE(TAG, "Failed to parse MCP message: %s", message.c_str());
        return;
    }
    ParseMessage(json);
    cJSON_Delete(json);
}

/**
 * @brief 解析客户端能力
 * @param capabilities cJSON格式的能力描述对象
 * @details 解析客户端发送的能力信息，目前主要处理视觉能力配置。
 * 如果客户端支持视觉功能，配置摄像头的图像分析URL和认证令牌。
 *
 * 支持的配置项：
 * - vision.url: 图像分析服务的URL
 * - vision.token: 访问令牌（可选）
 */
void McpServer::ParseCapabilities(const cJSON *capabilities)
{
    auto vision = cJSON_GetObjectItem(capabilities, "vision");
    if (cJSON_IsObject(vision))
    {
        auto url = cJSON_GetObjectItem(vision, "url");
        auto token = cJSON_GetObjectItem(vision, "token");
        if (cJSON_IsString(url))
        {
            auto camera = Board::GetInstance().GetCamera();
            if (camera)
            {
                std::string url_str = std::string(url->valuestring);
                std::string token_str;
                if (cJSON_IsString(token))
                {
                    token_str = std::string(token->valuestring);
                }
                camera->SetExplainUrl(url_str, token_str);
            }
        }
    }
}

/**
 * @brief 解析MCP协议消息（cJSON版本）
 * @param json 解析后的cJSON对象
 * @details 处理MCP协议的各种JSON-RPC请求，包括：
 * - initialize: 初始化连接，交换能力信息
 * - tools/list: 获取可用工具列表
 * - tools/call: 调用指定工具
 *
 * 消息格式验证：
 * 1. 检查jsonrpc版本必须为"2.0"
 * 2. 检查method字段存在且为字符串
 * 3. 跳过notifications开头的通知消息
 * 4. 检查params字段（如果存在）必须为对象
 * 5. 检查id字段存在且为数字
 *
 * @note 所有错误都会记录日志并通过ReplyError返回错误响应
 */
void McpServer::ParseMessage(const cJSON *json)
{
    // 检查JSONRPC版本
    auto version = cJSON_GetObjectItem(json, "jsonrpc");
    if (version == nullptr || !cJSON_IsString(version) || strcmp(version->valuestring, "2.0") != 0)
    {
        ESP_LOGE(TAG, "Invalid JSONRPC version: %s", version ? version->valuestring : "null");
        return;
    }

    // 检查method字段
    auto method = cJSON_GetObjectItem(json, "method");
    if (method == nullptr || !cJSON_IsString(method))
    {
        ESP_LOGE(TAG, "Missing method");
        return;
    }

    auto method_str = std::string(method->valuestring);
    // 跳过通知消息（无需响应）
    if (method_str.find("notifications") == 0)
    {
        return;
    }

    // 检查params字段
    auto params = cJSON_GetObjectItem(json, "params");
    if (params != nullptr && !cJSON_IsObject(params))
    {
        ESP_LOGE(TAG, "Invalid params for method: %s", method_str.c_str());
        return;
    }

    // 检查id字段
    auto id = cJSON_GetObjectItem(json, "id");
    if (id == nullptr || !cJSON_IsNumber(id))
    {
        ESP_LOGE(TAG, "Invalid id for method: %s", method_str.c_str());
        return;
    }
    auto id_int = id->valueint;

    // 处理不同的MCP方法
    if (method_str == "initialize")
    {
        // 初始化请求：解析客户端能力并返回服务器信息
        if (cJSON_IsObject(params))
        {
            auto capabilities = cJSON_GetObjectItem(params, "capabilities");
            if (cJSON_IsObject(capabilities))
            {
                ParseCapabilities(capabilities);
            }
        }
        auto app_desc = esp_app_get_description();
        std::string message = "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"" BOARD_NAME "\",\"version\":\"";
        message += app_desc->version;
        message += "\"}}";
        ReplyResult(id_int, message);
    }
    else if (method_str == "tools/list")
    {
        // 工具列表请求：返回所有可用工具
        std::string cursor_str = "";
        if (params != nullptr)
        {
            auto cursor = cJSON_GetObjectItem(params, "cursor");
            if (cJSON_IsString(cursor))
            {
                cursor_str = std::string(cursor->valuestring);
            }
        }
        GetToolsList(id_int, cursor_str);
    }
    else if (method_str == "tools/call")
    {
        // 工具调用请求：执行指定工具
        if (!cJSON_IsObject(params))
        {
            ESP_LOGE(TAG, "tools/call: Missing params");
            ReplyError(id_int, "Missing params");
            return;
        }
        auto tool_name = cJSON_GetObjectItem(params, "name");
        if (!cJSON_IsString(tool_name))
        {
            ESP_LOGE(TAG, "tools/call: Missing name");
            ReplyError(id_int, "Missing name");
            return;
        }
        auto tool_arguments = cJSON_GetObjectItem(params, "arguments");
        if (tool_arguments != nullptr && !cJSON_IsObject(tool_arguments))
        {
            ESP_LOGE(TAG, "tools/call: Invalid arguments");
            ReplyError(id_int, "Invalid arguments");
            return;
        }
        DoToolCall(id_int, std::string(tool_name->valuestring), tool_arguments);
    }
    else
    {
        // 未实现的方法
        ESP_LOGE(TAG, "Method not implemented: %s", method_str.c_str());
        ReplyError(id_int, "Method not implemented: " + method_str);
    }
}

/**
 * @brief 发送成功响应
 * @param id 请求ID
 * @param result 结果数据JSON字符串
 * @details 构造JSON-RPC 2.0格式的成功响应消息并通过Application发送。
 *
 * 响应格式：
 * @code
 * {"jsonrpc":"2.0","id":123,"result":{...}}
 * @endcode
 */
void McpServer::ReplyResult(int id, const std::string &result)
{
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id) + ",\"result\":";
    payload += result;
    payload += "}";
    Application::GetInstance().SendMcpMessage(payload);
}

/**
 * @brief 发送错误响应
 * @param id 请求ID
 * @param message 错误消息
 * @details 构造JSON-RPC 2.0格式的错误响应消息并通过Application发送。
 *
 * 响应格式：
 * @code
 * {"jsonrpc":"2.0","id":123,"error":{"message":"error description"}}
 * @endcode
 */
void McpServer::ReplyError(int id, const std::string &message)
{
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id);
    payload += ",\"error\":{\"message\":\"";
    payload += message;
    payload += "\"}}";
    Application::GetInstance().SendMcpMessage(payload);
}

/**
 * @brief 处理工具列表请求
 * @param id 请求ID
 * @param cursor 分页游标（工具名称）
 * @details 返回可用工具列表，支持分页以处理大量工具。
 *
 * 分页机制：
 * - 如果cursor为空，从第一个工具开始
 * - 如果cursor不为空，从指定工具开始
 * - 当响应大小接近max_payload_size时停止添加工具
 * - 返回nextCursor用于获取下一页
 *
 * 响应格式：
 * @code
 * {
 *     "tools": [...],
 *     "nextCursor": "tool_name"  // 可选，仅当有更多工具时
 * }
 * @endcode
 *
 * @note 跳过user_only=true的工具（对AI不可见）
 */
void McpServer::GetToolsList(int id, const std::string &cursor)
{
    const int max_payload_size = 8000; // 最大负载大小限制
    std::string json = "{\"tools\":[";

    bool found_cursor = cursor.empty();
    auto it = tools_.begin();
    std::string next_cursor = "";

    while (it != tools_.end())
    {
        // 跳过对AI不可见的工具
        if ((*it)->user_only())
        {
            ++it;
            continue;
        }

        // 如果我们还没有找到起始位置，继续搜索
        if (!found_cursor)
        {
            if ((*it)->name() == cursor)
            {
                found_cursor = true;
            }
            else
            {
                ++it;
                continue;
            }
        }

        // 添加tool前检查大小
        std::string tool_json = (*it)->to_json() + ",";
        if (json.length() + tool_json.length() + 30 > max_payload_size)
        {
            // 如果添加这个tool会超出大小限制，设置next_cursor并退出循环
            next_cursor = (*it)->name();
            break;
        }

        json += tool_json;
        ++it;
    }

    // 移除末尾的逗号
    if (json.back() == ',')
    {
        json.pop_back();
    }

    // 检查是否成功添加了任何工具
    if (json.back() == '[' && !tools_.empty())
    {
        ESP_LOGE(TAG, "tools/list: Failed to add tool %s because of payload size limit", next_cursor.c_str());
        ReplyError(id, "Failed to add tool " + next_cursor + " because of payload size limit");
        return;
    }

    // 构造最终响应
    if (next_cursor.empty())
    {
        json += "]}";
    }
    else
    {
        json += "],\"nextCursor\":\"" + next_cursor + "\"}";
    }

    ReplyResult(id, json);
}

/**
 * @brief 执行工具调用
 * @param id 请求ID
 * @param tool_name 要调用的工具名称
 * @param tool_arguments 工具参数（cJSON对象）
 * @details 查找并执行指定的工具，处理参数解析和验证。
 *
 * 执行流程：
 * 1. 在工具列表中查找指定名称的工具
 * 2. 解析并验证传入的参数
 * 3. 检查必需参数是否存在
 * 4. 在主线程中执行工具回调
 * 5. 返回执行结果
 *
 * 参数解析：
 * - 布尔值：cJSON_IsBool检查，valueint转换为bool
 * - 整数：cJSON_IsNumber检查，valueint获取值
 * - 字符串：cJSON_IsString检查，valuestring获取值
 *
 * 错误处理：
 * - 工具不存在：返回Unknown tool错误
 * - 缺少必需参数：返回Missing valid argument错误
 * - 参数类型不匹配：返回异常信息
 * - 执行异常：捕获并返回错误
 *
 * @note 工具在主线程中执行，避免多线程问题
 */
void McpServer::DoToolCall(int id, const std::string &tool_name, const cJSON *tool_arguments)
{
    // 在工具列表中查找指定工具
    auto tool_iter = std::find_if(tools_.begin(), tools_.end(),
                                  [&tool_name](const McpTool *tool)
                                  {
                                      return tool->name() == tool_name;
                                  });

    // 检查工具是否存在
    if (tool_iter == tools_.end())
    {
        ESP_LOGE(TAG, "tools/call: Unknown tool: %s", tool_name.c_str());
        ReplyError(id, "Unknown tool: " + tool_name);
        return;
    }

    // 复制工具的属性定义用于填充参数值
    PropertyList arguments = (*tool_iter)->properties();
    try
    {
        // 遍历所有属性，解析传入的参数值
        for (auto &argument : arguments)
        {
            bool found = false;
            if (cJSON_IsObject(tool_arguments))
            {
                auto value = cJSON_GetObjectItem(tool_arguments, argument.name().c_str());
                // 根据属性类型解析参数值
                if (argument.type() == kPropertyTypeBoolean && cJSON_IsBool(value))
                {
                    argument.set_value<bool>(value->valueint == 1);
                    found = true;
                }
                else if (argument.type() == kPropertyTypeInteger && cJSON_IsNumber(value))
                {
                    argument.set_value<int>(value->valueint);
                    found = true;
                }
                else if (argument.type() == kPropertyTypeString && cJSON_IsString(value))
                {
                    argument.set_value<std::string>(value->valuestring);
                    found = true;
                }
            }

            // 检查必需参数是否存在
            if (!argument.has_default_value() && !found)
            {
                ESP_LOGE(TAG, "tools/call: Missing valid argument: %s", argument.name().c_str());
                ReplyError(id, "Missing valid argument: " + argument.name());
                return;
            }
        }
    }
    catch (const std::exception &e)
    {
        // 参数解析异常
        ESP_LOGE(TAG, "tools/call: %s", e.what());
        ReplyError(id, e.what());
        return;
    }

    // 在主线程中执行工具（避免多线程问题）
    auto &app = Application::GetInstance();
    app.Schedule([this, id, tool_iter, arguments = std::move(arguments)]()
                 {
        try {
            // 执行工具并返回结果
            ReplyResult(id, (*tool_iter)->Call(arguments));
        } catch (const std::exception& e) {
            // 工具执行异常
            ESP_LOGE(TAG, "tools/call: %s", e.what());
            ReplyError(id, e.what());
        } });
}