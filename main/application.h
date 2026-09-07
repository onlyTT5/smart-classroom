/**
 * @file application.h
 * @brief 应用程序主类头文件
 * 
 * @details
 * 本文件定义了Application类，作为整个小智AI助手应用程序的核心控制器。
 * 负责管理设备状态、音频服务、网络协议、OTA升级、MCP消息处理等核心功能。
 * 
 * 核心功能模块：
 * - 设备状态管理（DeviceState）
 * - 音频服务控制（AudioService）
 * - 网络协议处理（Protocol）
 * - OTA升级管理
 * - MCP服务器消息处理
 * - LED控制器
 * - DHT11传感器
 * - MyInfo信息播报
 * 
 * 事件系统：
 * - MAIN_EVENT_SCHEDULE: 任务调度事件
 * - MAIN_EVENT_SEND_AUDIO: 发送音频数据事件
 * - MAIN_EVENT_WAKE_WORD_DETECTED: 唤醒词检测事件
 * - MAIN_EVENT_VAD_CHANGE: 语音活动检测变化事件
 * - MAIN_EVENT_ERROR: 错误事件
 * - MAIN_EVENT_CHECK_NEW_VERSION_DONE: 版本检查完成事件
 * - MAIN_EVENT_CLOCK_TICK: 时钟滴答事件
 * 
 * 使用示例：
 * @code
 * // 获取应用实例
 * auto& app = Application::GetInstance();
 * 
 * // 启动应用程序
 * app.Start();
 * 
 * // 调度任务到主线程
 * app.Schedule([]() {
 *     // 在主线程执行的代码
 * });
 * 
 * // 获取设备状态
 * auto state = app.GetDeviceState();
 * 
 * // 发送MCP消息
 * app.SendMcpMessage(json_payload);
 * @endcode
 * 
 * @note 使用单例模式，全局只有一个实例
 */

#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include <string>
#include <mutex>
#include <deque>
#include <memory>

#include "protocol.h"
#include "ota.h"
#include "audio_service.h"
#include "device_state_event.h"
#include "led_ctrl.h"
#include "dht11_sensor.h"
#include "myinfo.h"

// 主事件组标志位定义
#define MAIN_EVENT_SCHEDULE (1 << 0)           ///< 任务调度事件
#define MAIN_EVENT_SEND_AUDIO (1 << 1)         ///< 发送音频数据事件
#define MAIN_EVENT_WAKE_WORD_DETECTED (1 << 2) ///< 唤醒词检测事件
#define MAIN_EVENT_VAD_CHANGE (1 << 3)         ///< 语音活动检测变化事件
#define MAIN_EVENT_ERROR (1 << 4)              ///< 错误事件
#define MAIN_EVENT_CHECK_NEW_VERSION_DONE (1 << 5) ///< 版本检查完成事件
#define MAIN_EVENT_CLOCK_TICK (1 << 6)         ///< 时钟滴答事件

/**
 * @brief 回声消除模式枚举
 * @details 定义AEC（Acoustic Echo Cancellation）的工作模式
 */
enum AecMode {
    kAecOff,            ///< 关闭回声消除
    kAecOnDeviceSide,   ///< 设备端回声消除
    kAecOnServerSide,   ///< 服务器端回声消除
};

/**
 * @brief 应用程序主类
 * @details 管理整个小智AI助手应用程序的生命周期和核心功能。
 * 采用单例模式设计，提供统一的接口访问各种系统服务。
 * 
 * 主要职责：
 * - 设备状态管理（idle, listening, speaking等）
 * - 音频服务协调（录音、播放、唤醒词检测）
 * - 网络协议处理（WebSocket/MQTT通信）
 * - OTA固件升级
 * - 任务调度（Schedule机制）
 * - 外设控制（LED、传感器等）
 */
class Application {
public:
    /**
     * @brief 获取Application单例实例
     * @return Application实例的引用
     * @details 线程安全的单例实现（C++11及以上）
     */
    static Application& GetInstance() {
        static Application instance;
        return instance;
    }

    // 删除拷贝构造函数和赋值运算符（禁止拷贝）
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /**
     * @brief 启动应用程序
     * @details 初始化所有子系统并启动主事件循环
     * 
     * 启动流程：
     * 1. 初始化Board硬件
     * 2. 初始化显示
     * 3. 启动音频服务
     * 4. 启动网络协议
     * 5. 启动主事件循环
     */
    void Start();

    /**
     * @brief 主事件循环
     * @details 处理各种系统事件，包括：
     * - 任务调度（MAIN_EVENT_SCHEDULE）
     * - 音频发送（MAIN_EVENT_SEND_AUDIO）
     * - 唤醒词检测（MAIN_EVENT_WAKE_WORD_DETECTED）
     * - VAD变化（MAIN_EVENT_VAD_CHANGE）
     * - 错误处理（MAIN_EVENT_ERROR）
     * - 时钟滴答（MAIN_EVENT_CLOCK_TICK）
     * 
     * @note 此方法在单独的任务中运行，通常不需要直接调用
     */
    void MainEventLoop();

    /**
     * @brief 获取当前设备状态
     * @return 当前设备状态枚举值
     * @see DeviceState
     */
    DeviceState GetDeviceState() const { return device_state_; }

    /**
     * @brief 检查是否检测到语音
     * @return true表示检测到语音活动
     */
    bool IsVoiceDetected() const { return audio_service_.IsVoiceDetected(); }

    /**
     * @brief 调度任务到主线程执行
     * @param callback 要执行的回调函数
     * @details 线程安全的方法，将任务加入队列，由主事件循环执行。
     * 用于从其他线程安全地操作UI或音频服务。
     * 
     * 示例：
     * @code
     * app.Schedule([]() {
     *     // 在主线程执行的代码
     *     display->SetStatus("Done");
     * });
     * @endcode
     */
    void Schedule(std::function<void()> callback);

    /**
     * @brief 设置设备状态
     * @param state 新的设备状态
     * @details 更新设备状态并触发相应的状态变化处理
     */
    void SetDeviceState(DeviceState state);

    /**
     * @brief 显示警告/提示信息
     * @param status 状态标题
     * @param message 提示消息
     * @param emotion 表情图标（可选）
     * @param sound 提示音（可选）
     * @details 在屏幕上显示警告信息，并可选择播放提示音
     */
    void Alert(const char* status, const char* message, const char* emotion = "", const std::string_view& sound = "");

    /**
     * @brief 关闭警告显示
     * @details 关闭当前显示的警告/提示信息
     */
    void DismissAlert();

    /**
     * @brief 中止当前说话
     * @param reason 中止原因
     * @details 停止当前正在播放的语音输出
     */
    void AbortSpeaking(AbortReason reason);

    /**
     * @brief 切换聊天状态
     * @details 在监听和空闲状态之间切换
     */
    void ToggleChatState();

    /**
     * @brief 开始监听
     * @details 启动语音识别，准备接收用户语音输入
     */
    void StartListening();

    /**
     * @brief 停止监听
     * @details 停止语音识别，结束当前会话
     */
    void StopListening();

    /**
     * @brief 重启设备
     * @details 执行系统重启
     */
    void Reboot();

    /**
     * @brief 唤醒词触发
     * @param wake_word 检测到的唤醒词
     * @details 当检测到唤醒词时调用，触发相应的唤醒处理
     */
    void WakeWordInvoke(const std::string& wake_word);

    /**
     * @brief 检查是否可以进入睡眠模式
     * @return true表示可以进入睡眠模式
     * @details 根据当前状态判断设备是否可以进入低功耗模式
     */
    bool CanEnterSleepMode();

    /**
     * @brief 发送MCP消息
     * @param payload JSON格式的消息内容
     * @details 通过当前协议连接发送MCP（Model Context Protocol）消息
     */
    void SendMcpMessage(const std::string& payload);

    /**
     * @brief 设置回声消除模式
     * @param mode 回声消除模式
     * @see AecMode
     */
    void SetAecMode(AecMode mode);

    /**
     * @brief 获取当前回声消除模式
     * @return 当前AEC模式
     */
    AecMode GetAecMode() const { return aec_mode_; }

    /**
     * @brief 播放提示音
     * @param sound 提示音资源
     */
    void PlaySound(const std::string_view& sound);

    /**
     * @brief 获取音频服务实例
     * @return AudioService的引用
     */
    AudioService& GetAudioService() { return audio_service_; }

    /**
     * @brief 获取LED控制器实例
     * @return led_ctrl的引用
     */
    led_ctrl& GetLedCtrl() { return led_ctrl_; }

    /**
     * @brief 获取DHT11传感器实例
     * @return Dht11Sensor的引用
     */
    Dht11Sensor& GetDht11Sensor() { return dht11_sensor_; }

    /**
     * @brief 获取MyInfo实例
     * @return MyInfo的引用
     */
    MyInfo& GetMyInfo() { return my_info_; }

    /**
     * @brief 显示激活码
     * @param code 激活码
     * @param message 提示消息
     * @details 显示设备激活码供用户绑定设备
     */
    void ShowActivationCode(const std::string& code, const std::string& message);

private:
    /**
     * @brief 私有构造函数
     * @details 单例模式，禁止外部实例化
     */
    Application();

    /**
     * @brief 析构函数
     * @details 清理资源，停止定时器等
     */
    ~Application();

    std::mutex mutex_;                                          ///< 任务队列互斥锁
    std::deque<std::function<void()>> main_tasks_;             ///< 主任务队列
    std::unique_ptr<Protocol> protocol_;                        ///< 网络协议实例
    EventGroupHandle_t event_group_ = nullptr;                 ///< FreeRTOS事件组
    esp_timer_handle_t clock_timer_handle_ = nullptr;          ///< 时钟定时器句柄
    volatile DeviceState device_state_ = kDeviceStateUnknown; ///< 当前设备状态
    ListeningMode listening_mode_ = kListeningModeAutoStop;   ///< 监听模式
    AecMode aec_mode_ = kAecOff;                               ///< 回声消除模式
    std::string last_error_message_;                           ///< 最后错误消息
    AudioService audio_service_;                               ///< 音频服务实例

    bool has_server_time_ = false;                             ///< 是否已同步服务器时间
    bool aborted_ = false;                                     ///< 是否已中止
    int clock_ticks_ = 0;                                      ///< 时钟滴答计数
    TaskHandle_t check_new_version_task_handle_ = nullptr;     ///< 版本检查任务句柄
    TaskHandle_t main_event_loop_task_handle_ = nullptr;       ///< 主事件循环任务句柄
    led_ctrl led_ctrl_;                             ///< LED控制器实例
    Dht11Sensor dht11_sensor_;                                 ///< DHT11传感器实例
    MyInfo my_info_;                                           ///< MyInfo信息播报实例

    /**
     * @brief 唤醒词检测回调
     * @details 当检测到唤醒词时调用，触发状态切换
     */
    void OnWakeWordDetected();

    /**
     * @brief 检查新版本
     * @param ota OTA实例引用
     * @details 在后台任务中运行，检查固件更新并执行升级
     */
    void CheckNewVersion(Ota& ota);

    /**
     * @brief 设置监听模式
     * @param mode 监听模式
     * @see ListeningMode
     */
    void SetListeningMode(ListeningMode mode);
};


/**
 * @brief 任务优先级重置类
 * @details RAII风格的任务优先级管理工具，用于临时改变任务优先级。
 * 在构造时提升优先级，在析构时自动恢复原始优先级。
 * 
 * 使用场景：
 * - 需要临时提高任务优先级执行关键操作
 * - 确保操作完成后自动恢复优先级，避免遗漏
 * 
 * 示例：
 * @code
 * void CriticalOperation() {
 *     TaskPriorityReset priority_reset(configMAX_PRIORITIES - 1);  // 临时提升到最高优先级
 *     // 执行关键操作...
 * }  // 析构时自动恢复原始优先级
 * @endcode
 */
class TaskPriorityReset {
public:
    /**
     * @brief 构造函数
     * @param priority 临时设置的优先级
     * @details 保存当前优先级并设置为新优先级
     */
    TaskPriorityReset(BaseType_t priority) {
        original_priority_ = uxTaskPriorityGet(NULL);
        vTaskPrioritySet(NULL, priority);
    }

    /**
     * @brief 析构函数
     * @details 恢复原始任务优先级
     */
    ~TaskPriorityReset() {
        vTaskPrioritySet(NULL, original_priority_);
    }

private:
    BaseType_t original_priority_;  ///< 原始优先级
};

#endif // _APPLICATION_H_
