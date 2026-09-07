/**
 * @file display.h
 * @brief 显示屏基类头文件
 * 
 * @details
 * 本文件定义了显示系统的核心接口和基类，为ESP32-S3项目提供统一的显示抽象层。
 * 支持的功能包括：
 * - 状态栏显示（网络、电量、静音状态）
 * - 表情和图标显示
 * - 聊天消息显示
 * - 主题切换（亮色/暗色）
 * - 电源管理
 * - 通知提示
 * 
 * @note 这是一个抽象基类，具体的显示实现（LCD/OLED）需要继承此类
 * 
 * @author 虾哥开源项目
 * @version 1.9.4
 * @date 2024
 * 
 * @copyright MIT License
 * 
 * @section usage_example 使用示例
 * @code
 * // 获取显示实例（通常在Board类中初始化）
 * auto display = Board::GetInstance().GetDisplay();
 * 
 * // 设置状态文本
 * display->SetStatus("正在连接...");
 * 
 * // 显示表情
 * display->SetEmotion("happy");
 * 
 * // 显示聊天消息
 * display->SetChatMessage("assistant", "你好！我是小智AI。");
 * 
 * // 显示通知
 * display->ShowNotification("WiFi已连接", 3000);
 * 
 * // 更新状态栏
 * display->UpdateStatusBar(true);
 * 
 * // 切换主题
 * display->SetTheme("dark");
 * @endcode
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <lvgl.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <esp_pm.h>

#include <string>
#include <chrono>

/**
 * @brief 显示字体结构体
 * @details 包含文本字体、图标字体和表情字体三种字体配置
 * 
 * 使用场景：
 * - text_font: 用于显示普通文本内容
 * - icon_font: 用于显示状态栏图标（电池、WiFi等）
 * - emoji_font: 用于显示表情符号
 */
struct DisplayFonts {
    const lv_font_t* text_font = nullptr;    ///< 普通文本字体
    const lv_font_t* icon_font = nullptr;    ///< 图标字体（FontAwesome）
    const lv_font_t* emoji_font = nullptr;   ///< 表情字体
};

/**
 * @brief 显示基类
 * @details 
 * 提供统一的显示接口，是所有具体显示类（LcdDisplay, OledDisplay等）的基类。
 * 该类封装了LVGL的基本操作，提供线程安全的显示访问机制。
 * 
 * 主要功能模块：
 * 1. 状态显示：SetStatus, ShowNotification
 * 2. 表情图标：SetEmotion, SetIcon
 * 3. 消息显示：SetChatMessage
 * 4. 状态栏：UpdateStatusBar
 * 5. 主题管理：SetTheme, GetTheme
 * 6. 电源管理：SetPowerSaveMode
 * 
 * @note 所有LVGL操作都通过DisplayLockGuard实现线程安全
 * @see DisplayLockGuard
 */
class Display {
public:
    /**
     * @brief 构造函数
     * @details 
     * 初始化显示基类，创建：
     * - 通知定时器（用于自动隐藏通知）
     * - 电源管理锁（防止在显示更新时进入低功耗模式）
     */
    Display();
    
    /**
     * @brief 虚析构函数
     * @details 
     * 清理资源：
     * - 停止并删除通知定时器
     * - 删除LVGL对象
     * - 释放电源管理锁
     */
    virtual ~Display();

    /**
     * @brief 设置状态栏文本
     * @param status 要显示的文本内容
     * @details 
     * 在状态栏显示当前设备状态，如：
     * - "正在连接..."
     * - "待机中"
     * - "聆听中"
     * 
     * @note 调用后会自动隐藏通知标签，显示状态标签
     */
    virtual void SetStatus(const char* status);
    
    /**
     * @brief 显示通知（C字符串版本）
     * @param notification 通知内容
     * @param duration_ms 显示持续时间（毫秒），默认3000ms
     * @details 
     * 在状态栏位置显示临时通知，如：
     * - "WiFi已连接"
     * - "电量不足"
     * 
     * 通知会在指定时间后自动消失，恢复状态显示
     */
    virtual void ShowNotification(const char* notification, int duration_ms = 3000);
    
    /**
     * @brief 显示通知（std::string版本）
     * @param notification 通知内容
     * @param duration_ms 显示持续时间（毫秒），默认3000ms
     * @details 与C字符串版本功能相同，方便使用std::string
     */
    virtual void ShowNotification(const std::string &notification, int duration_ms = 3000);
    
    /**
     * @brief 设置表情
     * @param emotion 表情名称，如 "happy", "sad", "neutral"等
     * @details 
     * 在屏幕上显示对应的表情图标。
     * 支持的表情列表在子类中定义，通常包括：
     * - neutral（平静）
     * - happy（开心）
     * - sad（难过）
     * - angry（生气）
     * - surprised（惊讶）
     * - sleepy（困倦）等
     */
    virtual void SetEmotion(const char* emotion);
    
    /**
     * @brief 设置聊天消息
     * @param role 消息角色，如 "user", "assistant", "system"
     * @param content 消息内容
     * @details 
     * 显示对话消息内容。
     * - user: 用户说的话
     * - assistant: AI助手的回复
     * - system: 系统提示信息
     */
    virtual void SetChatMessage(const char* role, const char* content);
    
    /**
     * @brief 设置图标
     * @param icon 图标字符（FontAwesome图标）
     * @details 
     * 直接设置表情标签的图标字符。
     * 通常用于显示FontAwesome图标库中的图标。
     * @see font_awesome.h
     */
    virtual void SetIcon(const char* icon);
    
    /**
     * @brief 设置预览图片
     * @param image 图片描述符指针
     * @details 
     * 在屏幕上显示一张图片。
     * 基类实现为空，具体功能由子类实现。
     * 用于显示摄像头预览或接收到的图片。
     * 
     * @note 需要在子类中重写此函数以实现实际功能
     */
    virtual void SetPreviewImage(const lv_img_dsc_t* image);
    
    /**
     * @brief 设置主题
     * @param theme_name 主题名称，"light" 或 "dark"
     * @details 
     * 切换显示主题并保存到设置中。
     * - light: 浅色主题，适合明亮环境
     * - dark: 深色主题，适合暗光环境，更省电
     * 
     * @note 主题设置会持久化保存到NVS
     */
    virtual void SetTheme(const std::string& theme_name);
    
    /**
     * @brief 获取当前主题
     * @return 当前主题名称
     */
    virtual std::string GetTheme() { return current_theme_name_; }
    
    /**
     * @brief 更新状态栏
     * @param update_all 是否强制更新所有图标
     * @details 
     * 更新状态栏上的所有状态图标：
     * - 静音状态图标
     * - 电池电量图标
     * - 网络状态图标
     * - 时间显示
     * 
     * 通常由定时器定期调用，或在状态变化时调用。
     * 
     * @note 网络图标每10秒更新一次（除非update_all为true）
     */
    virtual void UpdateStatusBar(bool update_all = false);
    
    /**
     * @brief 设置省电模式
     * @param on true开启省电模式，false关闭
     * @details 
     * 进入或退出省电模式：
     * - 开启：清空聊天消息，显示"困倦"表情，降低屏幕亮度
     * - 关闭：清空聊天消息，显示"平静"表情，恢复正常亮度
     */
    virtual void SetPowerSaveMode(bool on);

    /**
     * @brief 启动屏保（如星空动画）
     * @details
     * 默认实现为空，具体显示类可根据需要重写此方法实现自定义屏保效果。
     */
    virtual void StartScreensaver() {}

    /**
     * @brief 停止屏保
     * @details
     * 默认实现为空，具体显示类可根据需要重写此方法来关闭屏保并恢复正常界面。
     */
    virtual void StopScreensaver() {}

    /**
     * @brief 获取屏幕宽度
     * @return 屏幕宽度（像素）
     */
    inline int width() const { return width_; }
    
    /**
     * @brief 获取屏幕高度
     * @return 屏幕高度（像素）
     */
    inline int height() const { return height_; }

protected:
    int width_ = 0;    ///< 屏幕宽度（像素）
    int height_ = 0;   ///< 屏幕高度（像素）
    
    esp_pm_lock_handle_t pm_lock_ = nullptr;  ///< 电源管理锁句柄
    lv_display_t *display_ = nullptr;         ///< LVGL显示设备句柄

    // LVGL对象指针
    lv_obj_t *emotion_label_ = nullptr;       ///< 表情/图标标签
    lv_obj_t *network_label_ = nullptr;       ///< 网络状态标签
    lv_obj_t *status_label_ = nullptr;        ///< 状态文本标签
    lv_obj_t *notification_label_ = nullptr;  ///< 通知文本标签
    lv_obj_t *mute_label_ = nullptr;          ///< 静音状态标签
    lv_obj_t *battery_label_ = nullptr;       ///< 电池状态标签
    lv_obj_t *brand_label_ = nullptr;         ///< 品牌标签（右上角显示"粤嵌"）
    lv_obj_t* chat_message_label_ = nullptr;  ///< 聊天消息标签
    lv_obj_t* low_battery_popup_ = nullptr;   ///< 低电量弹窗
    lv_obj_t* low_battery_label_ = nullptr;   ///< 低电量提示文本
    
    const char* battery_icon_ = nullptr;      ///< 当前电池图标
    const char* network_icon_ = nullptr;      ///< 当前网络图标
    bool muted_ = false;                      ///< 静音状态标志
    std::string current_theme_name_;          ///< 当前主题名称

    std::chrono::system_clock::time_point last_status_update_time_;  ///< 上次状态更新时间
    esp_timer_handle_t notification_timer_ = nullptr;                ///< 通知定时器句柄

    /**
     * @brief 声明DisplayLockGuard为友元类
     * @details 允许DisplayLockGuard访问Lock和Unlock方法
     */
    friend class DisplayLockGuard;
    
    /**
     * @brief 锁定显示（线程安全）
     * @param timeout_ms 超时时间（毫秒）
     * @return true锁定成功，false超时
     * @details 
     * 获取LVGL操作锁，防止多线程同时操作LVGL导致崩溃。
     * 所有LVGL操作前必须先调用Lock。
     * 
     * @note 建议使用DisplayLockGuard自动管理锁的生命周期
     */
    virtual bool Lock(int timeout_ms = 0) = 0;
    
    /**
     * @brief 解锁显示
     * @details 释放LVGL操作锁
     * @note 建议使用DisplayLockGuard自动管理锁的生命周期
     */
    virtual void Unlock() = 0;
};


/**
 * @brief 显示锁保护类（RAII模式）
 * @details 
 * 使用RAII模式自动管理显示锁的获取和释放。
 * 在构造函数中获取锁，在析构函数中释放锁。
 * 
 * 使用示例：
 * @code
 * void SomeFunction() {
 *     DisplayLockGuard lock(display);  // 获取锁
 *     // 执行LVGL操作...
 *     lv_label_set_text(label, "text");
 * }  // 函数结束时自动释放锁
 * @endcode
 * 
 * @warning 必须确保Lock在Unlock之前被调用，否则会导致死锁
 */
class DisplayLockGuard {
public:
    /**
     * @brief 构造函数，获取显示锁
     * @param display 显示对象指针
     * @details 
     * 尝试获取显示锁，超时时间为30秒。
     * 如果获取失败，会记录错误日志。
     */
    DisplayLockGuard(Display *display) : display_(display) {
        if (!display_->Lock(30000)) {
            ESP_LOGE("Display", "Failed to lock display");
        }
    }
    
    /**
     * @brief 析构函数，释放显示锁
     */
    ~DisplayLockGuard() {
        display_->Unlock();
    }

private:
    Display *display_;  ///< 关联的显示对象
};

/**
 * @brief 无显示实现类
 * @details 
 * 当系统没有显示屏时使用此类作为占位实现。
 * 所有显示操作都不执行任何操作，但提供相同的接口。
 * 
 * 使用场景：
 * - 调试时禁用显示
 * - 节省内存的极简配置
 * - 无屏设备（纯语音交互）
 */
class NoDisplay : public Display {
private:
    /**
     * @brief 锁定（空实现）
     * @return 总是返回true
     */
    virtual bool Lock(int timeout_ms = 0) override {
        return true;
    }
    
    /**
     * @brief 解锁（空实现）
     */
    virtual void Unlock() override {}
};

#endif // DISPLAY_H
