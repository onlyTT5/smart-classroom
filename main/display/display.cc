/**
 * @file display.cc
 * @brief 显示屏基类实现文件
 * 
 * @details
 * 本文件实现了Display基类的所有方法，提供显示系统的核心功能。
 * 主要功能包括：
 * - 状态栏管理和更新
 * - 通知显示和自动隐藏
 * - 表情和图标显示
 * - 聊天消息显示
 * - 主题管理
 * - 电源管理集成
 * - 电池和网络状态监控
 * 
 * @note 这是一个抽象基类的实现，具体的显示渲染由子类完成
 * 
 * @author 虾哥开源项目
 * @version 1.9.4
 * @date 2024
 * 
 * @copyright MIT License
 * 
 * @section architecture 架构说明
 * 
 * 显示系统采用分层架构：
 * @code
 * ┌─────────────────────────────────────────┐
 * │  Application Layer (应用层)              │
 * │  - 调用显示接口显示状态、消息等           │
 * ├─────────────────────────────────────────┤
 * │  Display Base Class (显示基类)           │
 * │  - 本文件实现                            │
 * │  - 提供统一的显示接口                     │
 * ├─────────────────────────────────────────┤
 * │  Concrete Display Classes (具体显示类)   │
 * │  - LcdDisplay (LCD显示屏)                │
 * │  - OledDisplay (OLED显示屏)              │
 * │  - NoDisplay (无显示)                    │
 * ├─────────────────────────────────────────┤
 * │  LVGL Library (图形库)                   │
 * │  - 底层图形渲染                          │
 * └─────────────────────────────────────────┘
 * @endcode
 * 
 * @section thread_safety 线程安全
 * 
 * 所有LVGL操作都通过Lock/Unlock机制保证线程安全：
 * - DisplayLockGuard: RAII风格的锁管理
 * - Lock(): 获取显示锁（超时30秒）
 * - Unlock(): 释放显示锁
 * 
 * @section state_machine 状态机
 * 
 * 状态栏显示状态：
 * - 正常状态: 显示SetStatus()设置的文本
 * - 通知状态: 显示ShowNotification()的通知，定时器到期后恢复
 * 
 * @section battery_levels 电池电量图标
 * 
 * 电池图标根据电量百分比自动切换：
 * - 0-19%:   空电池图标 (FONT_AWESOME_BATTERY_EMPTY)
 * - 20-39%:  1/4电量   (FONT_AWESOME_BATTERY_QUARTER)
 * - 40-59%:  1/2电量   (FONT_AWESOME_BATTERY_HALF)
 * - 60-79%:  3/4电量   (FONT_AWESOME_BATTERY_THREE_QUARTERS)
 * - 80-100%: 满电量    (FONT_AWESOME_BATTERY_FULL)
 * - 充电中:  闪电图标  (FONT_AWESOME_BATTERY_BOLT)
 */

#include <esp_log.h>
#include <esp_err.h>
#include <string>
#include <cstdlib>
#include <cstring>
#include <font_awesome.h>

#include "display.h"
#include "board.h"
#include "application.h"
#include "audio_codec.h"
#include "settings.h"
#include "assets/lang_config.h"

#define TAG "Display"  ///< 日志标签，用于ESP_LOG输出

/**
 * @brief 构造函数
 * @details 
 * 初始化显示基类，执行以下操作：
 * 1. 创建通知定时器，用于自动隐藏通知消息
 * 2. 创建电源管理锁，防止显示更新时进入低功耗模式
 * 
 * 通知定时器回调函数会在定时器到期时：
 * - 隐藏通知标签
 * - 显示状态标签
 */
Display::Display() {
    // 通知定时器配置
    esp_timer_create_args_t notification_timer_args = {
        .callback = [](void *arg) {
            // 定时器回调：隐藏通知，显示状态
            Display *display = static_cast<Display*>(arg);
            DisplayLockGuard lock(display);
            lv_obj_add_flag(display->notification_label_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(display->status_label_, LV_OBJ_FLAG_HIDDEN);
        },
        .arg = this,                          // 传递this指针作为回调参数
        .dispatch_method = ESP_TIMER_TASK,    // 在中断上下文执行
        .name = "notification_timer",         // 定时器名称
        .skip_unhandled_events = false,       // 不跳过未处理的事件
    };
    // 创建定时器
    ESP_ERROR_CHECK(esp_timer_create(&notification_timer_args, &notification_timer_));

    // 创建电源管理锁
    // 防止在显示更新时系统进入低功耗模式，避免显示闪烁
    auto ret = esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "display_update", &pm_lock_);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        // 如果电源管理不支持，记录信息日志
        ESP_LOGI(TAG, "Power management not supported");
    } else {
        // 其他错误则触发断言
        ESP_ERROR_CHECK(ret);
    }
}

/**
 * @brief 析构函数
 * @details 
 * 清理显示基类资源，执行以下操作：
 * 1. 停止并删除通知定时器
 * 2. 删除所有LVGL标签对象
 * 3. 删除低电量弹窗
 * 4. 释放电源管理锁
 */
Display::~Display() {
    // 清理通知定时器
    if (notification_timer_ != nullptr) {
        esp_timer_stop(notification_timer_);
        esp_timer_delete(notification_timer_);
    }

    // 删除LVGL标签对象
    if (network_label_ != nullptr) {
        lv_obj_del(network_label_);
        lv_obj_del(notification_label_);
        lv_obj_del(status_label_);
        lv_obj_del(mute_label_);
        lv_obj_del(battery_label_);
        lv_obj_del(emotion_label_);
    }
    // 删除低电量弹窗
    if( low_battery_popup_ != nullptr ) {
        lv_obj_del(low_battery_popup_);
    }
    // 释放电源管理锁
    if (pm_lock_ != nullptr) {
        esp_pm_lock_delete(pm_lock_);
    }
}

/**
 * @brief 设置状态栏文本
 * @param status 要显示的文本内容
 * @details 
 * 在状态栏显示当前设备状态。
 * 调用后会：
 * 1. 设置状态标签的文本
 * 2. 显示状态标签
 * 3. 隐藏通知标签
 * 4. 更新最后状态更新时间
 * 
 * 典型使用场景：
 * @code
 * display->SetStatus("正在连接服务器...");
 * display->SetStatus("聆听中");
 * display->SetStatus("说话中");
 * @endcode
 */
void Display::SetStatus(const char* status) {
    DisplayLockGuard lock(this);
    if (status_label_ == nullptr) {
        return;
    }
    // 设置状态文本
    lv_label_set_text(status_label_, status);
    // 显示状态标签，隐藏通知标签
    lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    // 记录状态更新时间，用于时钟显示逻辑
    last_status_update_time_ = std::chrono::system_clock::now();
}

/**
 * @brief 显示通知（C字符串版本）
 * @param notification 通知内容
 * @param duration_ms 显示持续时间（毫秒），默认3000ms
 * @details 
 * 在状态栏位置显示临时通知。
 * 
 * 执行流程：
 * 1. 设置通知标签文本
 * 2. 显示通知标签，隐藏状态标签
 * 3. 启动定时器，到期后自动恢复状态显示
 * 
 * 典型使用场景：
 * @code
 * display->ShowNotification("WiFi已连接", 2000);
 * display->ShowNotification("电量不足，请充电", 5000);
 * @endcode
 */
void Display::ShowNotification(const char* notification, int duration_ms) {
    DisplayLockGuard lock(this);
    if (notification_label_ == nullptr) {
        return;
    }
    // 设置通知文本
    lv_label_set_text(notification_label_, notification);
    // 显示通知标签，隐藏状态标签
    lv_obj_remove_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);

    // 重启定时器（如果正在运行则先停止）
    esp_timer_stop(notification_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(notification_timer_, duration_ms * 1000));
}

/**
 * @brief 显示通知（std::string版本）
 * @param notification 通知内容
 * @param duration_ms 显示持续时间（毫秒），默认3000ms
 * @details 
 * 与C字符串版本功能相同，方便使用std::string。
 * 内部调用C字符串版本实现。
 */
void Display::ShowNotification(const std::string &notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

/**
 * @brief 更新状态栏
 * @param update_all 是否强制更新所有图标
 * @details 
 * 更新状态栏上的所有状态图标。
 * 
 * 更新内容包括：
 * 1. 静音状态图标 - 根据音频编解码器音量状态
 * 2. 电池电量图标 - 根据电池电量和充电状态
 * 3. 网络状态图标 - 根据网络连接状态
 * 4. 时间显示 - 在空闲状态下显示当前时间
 * 
 * 网络图标更新策略：
 * - 每10秒更新一次（通过seconds_counter计数器）
 * - 或当update_all为true时立即更新
 * - 升级固件期间不更新4G网络状态（避免占用UART）
 * 
 * 低电量检测：
 * - 当电量低于20%且未充电时，显示低电量弹窗
 * - 播放低电量提示音
 * 
 * @note 函数内部使用esp_pm_lock_acquire/release保护显示更新
 */
void Display::UpdateStatusBar(bool update_all) {
    // 获取应用实例和板级实例
    auto& app = Application::GetInstance();
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();

    // 更新静音图标
    {
        DisplayLockGuard lock(this);
        if (mute_label_ == nullptr) {
            return;
        }

        // 如果静音状态改变，则更新图标
        if (codec->output_volume() == 0 && !muted_) {
            // 音量为0，显示静音图标
            muted_ = true;
            lv_label_set_text(mute_label_, FONT_AWESOME_VOLUME_XMARK);
        } else if (codec->output_volume() > 0 && muted_) {
            // 音量恢复，清除静音图标
            muted_ = false;
            lv_label_set_text(mute_label_, "");
        }
    }

    // 更新时间显示（仅在空闲状态下）
    if (app.GetDeviceState() == kDeviceStateIdle) {
        // 检查是否超过10秒未更新状态
        if (last_status_update_time_ + std::chrono::seconds(10) < std::chrono::system_clock::now()) {
            // 设置状态为当前时间 "HH:MM"
            time_t now = time(NULL);
            struct tm* tm = localtime(&now);
            // 检查系统时间是否已设置（年份>=2025）
            if (tm->tm_year >= 2025 - 1900) {
                char time_str[16];
                strftime(time_str, sizeof(time_str), "%H:%M  ", tm);
                SetStatus(time_str);
            } else {
                ESP_LOGW(TAG, "System time is not set, tm_year: %d", tm->tm_year);
            }
        }
    }

    // 获取电源管理锁，防止更新期间进入低功耗
    esp_pm_lock_acquire(pm_lock_);
    
    // 更新电池图标
    int battery_level;
    bool charging, discharging;
    const char* icon = nullptr;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        // 根据充电状态选择图标
        if (charging) {
            icon = FONT_AWESOME_BATTERY_BOLT;  // 充电中
        } else {
            // 根据电量百分比选择对应图标
            const char* levels[] = {
                FONT_AWESOME_BATTERY_EMPTY,          // 0-19%
                FONT_AWESOME_BATTERY_QUARTER,        // 20-39%
                FONT_AWESOME_BATTERY_HALF,           // 40-59%
                FONT_AWESOME_BATTERY_THREE_QUARTERS, // 60-79%
                FONT_AWESOME_BATTERY_FULL,           // 80-99%
                FONT_AWESOME_BATTERY_FULL,           // 100%
            };
            icon = levels[battery_level / 20];
        }
        // 更新电池标签
        DisplayLockGuard lock(this);
        if (battery_label_ != nullptr && battery_icon_ != icon) {
            battery_icon_ = icon;
            lv_label_set_text(battery_label_, battery_icon_);
        }

        // 低电量弹窗处理
        if (low_battery_popup_ != nullptr) {
            // 电量为空且正在放电，显示低电量警告
            if (strcmp(icon, FONT_AWESOME_BATTERY_EMPTY) == 0 && discharging) {
                if (lv_obj_has_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN)) {
                    // 显示低电量弹窗
                    lv_obj_remove_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
                    // 播放低电量提示音
                    app.PlaySound(Lang::Sounds::OGG_LOW_BATTERY);
                }
            } else {
                // 电量恢复，隐藏低电量弹窗
                if (!lv_obj_has_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN)) {
                    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }

    // 每10秒更新一次网络图标
    static int seconds_counter = 0;
    if (update_all || seconds_counter++ % 10 == 0) {
        // 升级固件时，不读取4G网络状态，避免占用UART资源
        auto device_state = Application::GetInstance().GetDeviceState();
        // 定义允许读取网络状态的状态列表
        static const std::vector<DeviceState> allowed_states = {
            kDeviceStateIdle,
            kDeviceStateStarting,
            kDeviceStateWifiConfiguring,
            kDeviceStateListening,
            kDeviceStateActivating,
        };
        // 仅在允许的状态下更新网络图标
        if (std::find(allowed_states.begin(), allowed_states.end(), device_state) != allowed_states.end()) {
            icon = board.GetNetworkStateIcon();
            if (network_label_ != nullptr && icon != nullptr && network_icon_ != icon) {
                DisplayLockGuard lock(this);
                network_icon_ = icon;
                lv_label_set_text(network_label_, network_icon_);
            }
        }
    }

    // 释放电源管理锁
    esp_pm_lock_release(pm_lock_);
}


/**
 * @brief 设置表情
 * @param emotion 表情名称，如 "happy", "sad", "neutral"等
 * @details 
 * 在屏幕上显示对应的表情图标。
 * 
 * 实现逻辑：
 * 1. 通过font_awesome_get_utf8()获取表情对应的UTF8字符
 * 2. 如果找到对应图标，调用SetIcon()显示
 * 3. 如果未找到，显示默认的neutral表情
 * 
 * 支持的表情列表在font_awesome.h中定义。
 * 
 * 典型使用场景：
 * @code
 * display->SetEmotion("happy");    // 开心
 * display->SetEmotion("sad");      // 难过
 * display->SetEmotion("surprised"); // 惊讶
 * @endcode
 */
void Display::SetEmotion(const char* emotion) {
    // 获取表情对应的UTF8字符
    const char* utf8 = font_awesome_get_utf8(emotion);
    if (utf8 != nullptr) {
        SetIcon(utf8);
    } else {
        // 未找到对应表情，显示默认neutral
        SetIcon(FONT_AWESOME_NEUTRAL);
    }
}

/**
 * @brief 设置图标
 * @param icon 图标字符（FontAwesome图标）
 * @details 
 * 直接设置表情标签的图标字符。
 * 
 * 使用场景：
 * - 显示FontAwesome图标
 * - 显示自定义表情
 * 
 * @code
 * display->SetIcon(FONT_AWESOME_WIFI);
 * display->SetIcon("😊");
 * @endcode
 */
void Display::SetIcon(const char* icon) {
    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return;
    }
    lv_label_set_text(emotion_label_, icon);
}

/**
 * @brief 设置预览图片
 * @param image 图片描述符指针
 * @details 
 * 基类实现为空操作。
 * 
 * 子类（如LcdDisplay）需要重写此函数以实现实际的图片显示功能。
 * 用于显示摄像头预览或接收到的图片。
 * 
 * @note 需要在子类中重写此函数
 * @see LcdDisplay::SetPreviewImage
 */
void Display::SetPreviewImage(const lv_img_dsc_t* image) {
    // 基类空实现，由子类实现具体功能
}

/**
 * @brief 设置聊天消息
 * @param role 消息角色，如 "user", "assistant", "system"
 * @param content 消息内容
 * @details 
 * 显示对话消息内容。
 * 
 * 基类实现仅简单设置聊天消息标签的文本。
 * 子类（如LcdDisplay）可以重写此函数以实现更复杂的消息显示效果
 * （如气泡样式、不同颜色等）。
 * 
 * 角色说明：
 * - user: 用户说的话，通常显示在右侧
 * - assistant: AI助手的回复，通常显示在左侧
 * - system: 系统提示信息，通常居中显示
 * 
 * @code
 * display->SetChatMessage("user", "你好！");
 * display->SetChatMessage("assistant", "你好！我是小智AI。");
 * display->SetChatMessage("system", "正在连接...");
 * @endcode
 */
void Display::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        return;
    }
    lv_label_set_text(chat_message_label_, content);
}

/**
 * @brief 设置主题
 * @param theme_name 主题名称，"light" 或 "dark"
 * @details 
 * 切换显示主题并保存到NVS设置中。
 * 
 * 主题切换会影响：
 * - 背景颜色
 * - 文本颜色
 * - 气泡颜色
 * - 边框颜色
 * 
 * 主题设置会持久化保存，下次启动时自动恢复。
 * 
 * @code
 * display->SetTheme("light");  // 浅色主题
 * display->SetTheme("dark");   // 深色主题
 * @endcode
 */
void Display::SetTheme(const std::string& theme_name) {
    current_theme_name_ = theme_name;
    // 保存到NVS设置
    Settings settings("display", true);
    settings.SetString("theme", theme_name);
}

/**
 * @brief 设置省电模式
 * @param on true开启省电模式，false关闭
 * @details 
 * 进入或退出省电模式。
 * 
 * 开启省电模式时：
 * - 清空聊天消息
 * - 显示"困倦"表情
 * - 降低屏幕亮度（由子类实现）
 * 
 * 关闭省电模式时：
 * - 清空聊天消息
 * - 显示"平静"表情
 * - 恢复正常亮度（由子类实现）
 * 
 * 典型使用场景：
 * - 长时间无操作时自动进入省电模式
 * - 用户按键唤醒时退出省电模式
 * 
 * @code
 * display->SetPowerSaveMode(true);   // 进入省电模式
 * display->SetPowerSaveMode(false);  // 退出省电模式
 * @endcode
 */
void Display::SetPowerSaveMode(bool on) {
    if (on) {
        // 开启省电模式
        SetChatMessage("system", "");  // 清空聊天消息
        SetEmotion("sleepy");           // 显示困倦表情
    } else {
        // 退出省电模式
        SetChatMessage("system", "");  // 清空聊天消息
        SetEmotion("neutral");          // 显示平静表情
    }
}
