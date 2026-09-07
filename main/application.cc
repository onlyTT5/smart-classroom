/**
 * @file application.cc
 * @brief 应用程序主类实现文件
 * 
 * @details
 * 本文件实现了Application类的所有方法，是小智AI助手应用程序的核心实现。
 * 负责设备启动、状态管理、音频处理、网络通信、OTA升级等功能的协调。
 * 
 * 核心功能模块：
 * - 应用程序生命周期管理（构造函数/析构函数/Start）
 * - 主事件循环处理（MainEventLoop）
 * - 设备状态管理（SetDeviceState/GetDeviceState）
 * - 音频服务协调（音频采集、播放、唤醒词检测）
 * - 网络协议处理（WebSocket/MQTT连接管理）
 * - OTA固件升级（CheckNewVersion）
 * - 设备激活流程（ShowActivationCode）
 * - 任务调度机制（Schedule）
 * 
 * 状态机：
 * - kDeviceStateUnknown: 未知状态
 * - kDeviceStateStarting: 启动中
 * - kDeviceStateConfiguring: 配置中
 * - kDeviceStateIdle: 空闲
 * - kDeviceStateConnecting: 连接中
 * - kDeviceStateListening: 监听中
 * - kDeviceStateSpeaking: 说话中
 * - kDeviceStateUpgrading: 升级中
 * - kDeviceStateActivating: 激活中
 * 
 * 事件处理：
 * - MAIN_EVENT_SCHEDULE: 执行调度任务
 * - MAIN_EVENT_SEND_AUDIO: 发送音频数据到服务器
 * - MAIN_EVENT_WAKE_WORD_DETECTED: 处理唤醒词检测
 * - MAIN_EVENT_VAD_CHANGE: 处理语音活动检测变化
 * - MAIN_EVENT_ERROR: 处理错误情况
 * - MAIN_EVENT_CLOCK_TICK: 处理时钟滴答（状态栏更新）
 */

#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>

#define TAG "Application"  ///< ESP日志标签


/**
 * @brief 设备状态字符串映射表
 * @details 将DeviceState枚举值映射为可读的字符串，用于日志输出和调试
 */
static const char* const STATE_STRINGS[] = {
    "unknown",        ///< kDeviceStateUnknown
    "starting",       ///< kDeviceStateStarting
    "configuring",    ///< kDeviceStateConfiguring
    "idle",           ///< kDeviceStateIdle
    "connecting",     ///< kDeviceStateConnecting
    "listening",      ///< kDeviceStateListening
    "speaking",       ///< kDeviceStateSpeaking
    "upgrading",      ///< kDeviceStateUpgrading
    "activating",     ///< kDeviceStateActivating
    "audio_testing",  ///< kDeviceStateAudioTesting
    "fatal_error",    ///< kDeviceStateFatalError
    "invalid_state"   ///< 无效状态
};

/**
 * @brief 构造函数
 * @details 初始化Application实例，创建事件组和时钟定时器。
 * 
 * 初始化流程：
 * 1. 创建FreeRTOS事件组（event_group_）
 * 2. 根据编译配置设置AEC模式（设备端/服务器端/关闭）
 * 3. 创建时钟定时器（每秒触发一次MAIN_EVENT_CLOCK_TICK）
 * 
 * AEC模式配置：
 * - CONFIG_USE_DEVICE_AEC: 使用设备端回声消除
 * - CONFIG_USE_SERVER_AEC: 使用服务器端回声消除
 * - 两者都未定义: 关闭回声消除
 * 
 * @note 时钟定时器在Start()方法中启动
 */
Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    // 创建时钟定时器参数
    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

/**
 * @brief 析构函数
 * @details 清理Application资源，停止定时器并删除事件组。
 * 
 * 清理流程：
 * 1. 停止并删除时钟定时器
 * 2. 删除FreeRTOS事件组
 */
Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

/**
 * @brief 检查新版本并执行OTA升级
 * @param ota OTA实例引用
 * @details 在后台任务中运行，执行以下操作：
 * 1. 检查服务器是否有新版本固件
 * 2. 如有新版本，下载并执行升级
 * 3. 处理设备激活流程
 * 
 * 重试机制：
 * - 最大重试次数：10次
 * - 初始重试延迟：10秒
 * - 每次失败后延迟时间翻倍（指数退避）
 * 
 * 升级流程：
 * 1. 显示升级提示
 * 2. 退出省电模式
 * 3. 停止音频服务
 * 4. 执行固件下载和烧录
 * 5. 升级成功后重启设备
 * 6. 升级失败则恢复音频服务继续运行
 * 
 * 激活流程：
 * - 显示激活码供用户绑定
 * - 等待用户完成激活
 * - 最多尝试10次激活
 */
void Application::CheckNewVersion(Ota& ota) {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒

    auto& board = Board::GetInstance();
    while (true) {
        SetDeviceState(kDeviceStateActivating);
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        // 检查版本信息
        if (!ota.CheckVersion()) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, ota.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // 每次重试后延迟时间翻倍
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // 重置重试延迟时间

        // 有新版本，执行升级
        if (ota.HasNewVersion()) {
            Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);

            vTaskDelay(pdMS_TO_TICKS(3000));

            SetDeviceState(kDeviceStateUpgrading);
            
            std::string message = std::string(Lang::Strings::NEW_VERSION) + ota.GetFirmwareVersion();
            display->SetChatMessage("system", message.c_str());

            board.SetPowerSaveMode(false);
            audio_service_.Stop();
            vTaskDelay(pdMS_TO_TICKS(1000));

            // 开始固件升级
            bool upgrade_success = ota.StartUpgrade([display](int progress, size_t speed) {
                std::thread([display, progress, speed]() {
                    char buffer[32];
                    snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                    display->SetChatMessage("system", buffer);
                }).detach();
            });

            if (!upgrade_success) {
                // 升级失败，恢复音频服务继续运行
                ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
                audio_service_.Start(); // 重启音频服务
                board.SetPowerSaveMode(true); // 恢复省电模式
                Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
                vTaskDelay(pdMS_TO_TICKS(3000));
                // 继续正常运行
            } else {
                // 升级成功，立即重启
                ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
                display->SetChatMessage("system", "Upgrade successful, rebooting...");
                vTaskDelay(pdMS_TO_TICKS(1000)); // 短暂显示消息
                Reboot();
                return; // 重启后不会执行到这里
            }
        }

        // 没有新版本，标记当前版本为有效
        ota.MarkCurrentVersionValid();
        if (!ota.HasActivationCode() && !ota.HasActivationChallenge()) {
            xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
            // 版本检查完成，退出循环
            break;
        }

        // 显示激活界面
        display->SetStatus(Lang::Strings::ACTIVATION);
        // 显示激活码供用户输入
        if (ota.HasActivationCode()) {
            ShowActivationCode(ota.GetActivationCode(), ota.GetActivationMessage());
        }

        // 等待激活完成或超时
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota.Activate();
            if (err == ESP_OK) {
                xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (device_state_ == kDeviceStateIdle) {
                break;
            }
        }
    }
}

/**
 * @brief 显示设备激活码
 * @param code 激活码字符串
 * @param message 提示消息
 * @details 显示激活码并在屏幕上显示提示信息，同时语音播报激活码。
 * 用于设备首次使用时的用户绑定流程。
 * 
 * 播报方式：
 * - 逐个数字播报
 * - 每个数字对应一个OGG音频文件
 * - 使用AudioService播放
 * 
 * @note 激活提示语音占用约9KB SRAM，需要等待播放完成
 */
void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    // 数字到音频的映射结构
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // 显示激活提示（占用9KB SRAM，需等待完成）
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    // 逐个播报激活码数字
    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

/**
 * @brief 显示警告/提示信息
 * @param status 状态标题
 * @param message 提示消息内容
 * @param emotion 表情图标名称（可选）
 * @param sound 提示音资源（可选）
 * @details 在屏幕上显示警告信息，并可选择播放提示音。
 * 同时记录日志到ESP日志系统。
 * 
 * 显示内容：
 * - 状态栏显示status
 * - 表情区域显示emotion图标
 * - 聊天区域显示message
 * 
 * @note 如果sound不为空，会通过audio_service_播放
 */
void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

/**
 * @brief 关闭警告显示
 * @details 关闭当前显示的警告/提示信息，恢复到待机状态。
 * 仅在设备处于空闲状态(kDeviceStateIdle)时执行恢复操作。
 * 
 * 恢复后的显示状态：
 * - 状态栏显示待机字符串
 * - 表情显示为neutral
 * - 聊天区域清空
 */
void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

/**
 * @brief 切换聊天状态
 * @details 根据当前设备状态执行不同的状态切换操作：
 * 
 * 状态切换逻辑：
 * - Activating -> Idle: 跳过激活流程
 * - WifiConfiguring -> AudioTesting: 进入音频测试模式
 * - AudioTesting -> WifiConfiguring: 退出音频测试模式
 * - Idle -> Listening: 开始对话（自动停止模式）
 * - Speaking -> Idle: 中止说话
 * - Listening -> Idle: 停止监听
 * 
 * 监听模式选择：
 * - AEC关闭: 使用kListeningModeAutoStop（自动停止）
 * - AEC开启: 使用kListeningModeRealtime（实时模式）
 */
void Application::ToggleChatState() {
    // 在激活状态下切换，直接进入空闲状态
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        // 进入音频测试模式
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (device_state_ == kDeviceStateAudioTesting) {
        // 退出音频测试模式
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    // 空闲状态：开始对话
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            // 打开音频通道
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            // 根据AEC模式选择监听模式
            SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        // 说话状态：中止说话
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {
        // 监听状态：停止监听
        Schedule([this]() {
            protocol_->CloseAudioChannel();
        });
    }
}

/**
 * @brief 开始监听
 * @details 启动语音识别，准备接收用户语音输入。
 * 使用手动停止模式（kListeningModeManualStop），需要显式调用StopListening停止。
 * 
 * 状态切换逻辑：
 * - Activating -> Idle: 跳过激活
 * - WifiConfiguring -> AudioTesting: 进入音频测试
 * - Idle -> Listening: 开始监听
 * - Speaking -> Listening: 中止说话并开始监听
 * 
 * @note 与ToggleChatState不同，此方法使用手动停止模式
 * @see StopListening()
 */
void Application::StartListening() {
    // 在激活状态下，直接进入空闲状态
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        // 进入音频测试模式
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    // 空闲状态：开始监听
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            // 打开音频通道
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            // 使用手动停止模式
            SetListeningMode(kListeningModeManualStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        // 说话状态：中止说话并开始监听
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop);
        });
    }
}

/**
 * @brief 停止监听
 * @details 停止语音识别，结束当前会话。
 * 
 * 状态切换逻辑：
 * - AudioTesting -> WifiConfiguring: 退出音频测试模式
 * - Listening -> Idle: 发送停止监听命令并进入空闲状态
 * - Speaking/Idle: 忽略（已在处理中或已空闲）
 * 
 * 有效状态检查：
 * 仅在Listening、Speaking、Idle状态下执行操作
 */
void Application::StopListening() {
    // 在音频测试状态下，退出测试模式
    if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    // 定义有效状态列表
    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // 如果当前状态不在有效列表中，不执行任何操作
    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end()) {
        return;
    }

    // 调度停止监听任务
    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            // 发送停止监听命令
            protocol_->SendStopListening();
            // 进入空闲状态
            SetDeviceState(kDeviceStateIdle);
        }
    });
}

/**
 * @brief 启动应用程序
 * @details 初始化所有子系统并启动主事件循环。
 * 
 * 启动流程：
 * 1. 获取Board实例并设置启动状态
 * 2. 初始化显示屏并显示版本信息
 * 3. 初始化并启动音频服务
 * 4. 初始化LED控制器
 * 5. 初始化DHT11传感器
 * 6. 初始化MyInfo模块
 * 7. 设置音频服务回调函数
 * 8. 启动主事件循环任务
 * 9. 启动时钟定时器
 * 10. 启动网络服务
 * 11. 执行OTA版本检查和设备激活
 * 12. 初始化通信协议（MQTT/WebSocket）
 * 13. 设置协议回调函数
 * 
 * 协议回调设置：
 * - OnConnected: 连接成功处理
 * - OnNetworkError: 网络错误处理
 * - OnIncomingAudio: 接收音频数据处理
 * - OnAudioChannelOpened: 音频通道打开处理
 * - OnAudioChannelClosed: 音频通道关闭处理
 * - OnIncomingJson: JSON消息处理（TTS/STT/MCP等）
 */
void Application::Start() {
    // 获取Board实例并设置启动状态
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* 初始化显示屏 */
    auto display = board.GetDisplay();
    // 显示版本信息
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    /* 初始化音频服务 */
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    /* 初始化LED控制器 */
    esp_err_t ret = led_ctrl_.led_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED controller: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "LED controller initialized successfully");
    }

    /* 初始化DHT11温湿度传感器 */
    if (!dht11_sensor_.Initialize()) {
        ESP_LOGE(TAG, "Failed to initialize DHT11 sensor");
    } else {
        ESP_LOGI(TAG, "DHT11 sensor initialized successfully");
    }

    /* 初始化MyInfo模块 */
    if (my_info_.Initialize() != MyInfoError::SUCCESS) {
        ESP_LOGE(TAG, "Failed to initialize MyInfo module");
    } else {
        ESP_LOGI(TAG, "MyInfo module initialized successfully");
        // 设置语音播放回调
        my_info_.SetPlayVoiceCallback([this](const std::string& text) {
            this->PlaySound(text);
        });
    }

    // 设置音频服务回调
    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    // 启动主事件循环任务（优先级3，栈大小8KB）
    xTaskCreate([](void* arg) {
        ((Application*)arg)->MainEventLoop();
        vTaskDelete(NULL);
    }, "main_event_loop", 2048 * 4, this, 3, &main_event_loop_task_handle_);

    // 启动时钟定时器（每秒触发一次）
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    // 启动网络服务
    board.StartNetwork();
    display->UpdateStatusBar(true);

    // OTA版本检查和设备激活
    Ota ota;
    CheckNewVersion(ota);

    // 初始化通信协议
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // 添加MCP工具
    McpServer::GetInstance().AddCommonTools();

    // 根据配置选择协议
#if 0
    if (ota.HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota.HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }
#else
    protocol_ = std::make_unique<WebsocketProtocol>();
#endif
    // 设置协议回调
    protocol_->OnConnected([this]() {
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });

    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (device_state_ == kDeviceStateSpeaking) {
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        }
    });

    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveMode(false);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });

    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveMode(true);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });

    // 处理JSON消息（TTS/STT/MCP等）
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    if (device_state_ == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([this, display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                Schedule([this, display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("user", message.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // 如果用户请求OTA更新，执行重启
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    bool protocol_started = protocol_->Start();

    // 打印堆内存统计
    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota.HasServerTime();
    if (protocol_started) {
        std::string message = std::string(Lang::Strings::VERSION) + ota.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        // 播放成功提示音，表示设备已就绪
        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
    }
}

/**
 * @brief 调度异步任务到主事件循环
 * @param callback 要执行的回调函数
 * @details 将任务添加到主任务队列，并通过事件组通知主事件循环执行。
 * 
 * 线程安全：
 * - 使用互斥锁保护任务队列
 * - 支持多线程安全调用
 * 
 * 使用场景：
 * - 子线程需要更新UI时
 * - 异步操作完成后需要回到主线程处理
 * - 延迟执行任务
 */
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

/**
 * @brief 主事件循环
 * @details 应用程序的核心事件处理循环，负责协调所有子系统的运行。
 * 
 * 处理的事件类型：
 * - MAIN_EVENT_SCHEDULE: 执行调度任务队列中的任务
 * - MAIN_EVENT_SEND_AUDIO: 发送音频数据到服务器
 * - MAIN_EVENT_WAKE_WORD_DETECTED: 处理唤醒词检测
 * - MAIN_EVENT_VAD_CHANGE: 处理语音活动检测变化
 * - MAIN_EVENT_CLOCK_TICK: 处理时钟滴答（每秒触发）
 * - MAIN_EVENT_ERROR: 处理错误情况
 * 
 * 设计原则：
 * - 所有对UI和协议状态的访问都通过此循环
 * - 其他任务需要使用Schedule()方法将操作提交到主循环
 * - 使用事件组实现高效的等待机制
 * 
 * @note 此函数在独立的FreeRTOS任务中运行，优先级为3
 * @see Schedule()
 */
void Application::MainEventLoop() {
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED |
            MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_CLOCK_TICK |
            MAIN_EVENT_ERROR, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (!protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            OnWakeWordDetected();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (device_state_ == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();

            // 空闲状态下一段时间后启动星空屏保，其他状态则关闭屏保
            if (device_state_ == kDeviceStateIdle) {
                // 例如空闲满15秒后启动屏保
                if (clock_ticks_ == 15) {
                    display->StartScreensaver();
                }
            } else {
                display->StopScreensaver();
            }

            // 每10秒打印调试信息
            if (clock_ticks_ % 10 == 0) {
                // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));  // 打印任务CPU使用率
                // SystemInfo::PrintTaskList();  // 打印任务列表
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

/**
 * @brief 唤醒词检测回调处理
 * @details 当检测到唤醒词时调用，处理状态切换和音频通道管理。
 * 
 * 处理流程：
 * 1. 检查协议是否已初始化
 * 2. 调用DHT11传感器的OnWakeUp方法，动态调整温度值
 * 3. 根据当前状态执行不同操作：
 *    - Idle状态：打开音频通道，开始监听
 *    - Speaking状态：中止说话
 *    - Activating状态：跳过激活流程
 * 
 * 唤醒词数据处理：
 * - 如果启用了AFE或自定义唤醒词，编码并发送唤醒词数据
 * - 发送唤醒词检测到的事件给服务器
 * 
 * @note 此函数在主事件循环中被调用
 * @see AudioService::SetCallbacks()
 */
void Application::OnWakeWordDetected() {
    if (!protocol_) {
        return;
    }

    // 调用DHT11传感器的OnWakeUp方法，动态调整温度值
    dht11_sensor_.OnWakeUp();

    if (device_state_ == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD || CONFIG_USE_CUSTOM_WAKE_WORD
        // 编码并发送唤醒词数据到服务器
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // 设置聊天状态为唤醒词检测到
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        // 播放弹出提示音，表示唤醒词已检测到
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
    } else if (device_state_ == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
    }
}

/**
 * @brief 中止说话
 * @param reason 中止原因
 * @details 在说话状态下中止当前的语音输出。
 * 
 * 应用场景：
 * - 用户打断说话（通过唤醒词或按键）
 * - 需要切换到其他状态
 * 
 * 操作：
 * 1. 设置中止标志
 * 2. 向服务器发送中止说话命令
 * 
 * @note 中止后设备通常会进入监听状态
 */
void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    protocol_->SendAbortSpeaking(reason);
}

/**
 * @brief 设置监听模式
 * @param mode 监听模式（自动停止/手动停止/实时）
 * @details 配置语音识别的监听模式并切换到监听状态。
 * 
 * 监听模式说明：
 * - kListeningModeAutoStop: 自动停止模式，检测到静音后自动结束
 * - kListeningModeManualStop: 手动停止模式，需要显式调用StopListening()
 * - kListeningModeRealtime: 实时模式，持续监听，适合连续对话
 * 
 * @see ListeningMode
 * @see StopListening()
 */
void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

/**
 * @brief 设置设备状态
 * @param state 新的设备状态
 * @details 核心状态机管理函数，处理状态切换时的各种操作。
 * 
 * 状态切换处理：
 * 1. 重置时钟计数器（用于屏保计时）
 * 2. 记录状态变化日志
 * 3. 发送状态变化事件通知
 * 4. 更新LED状态
 * 5. 根据新状态执行相应的UI和音频配置
 * 
 * 各状态处理：
 * - Idle: 待机状态，启用唤醒词检测，禁用语音处理
 * - Connecting: 连接中，清空聊天消息
 * - Listening: 监听中，启用语音处理，禁用唤醒词检测
 * - Speaking: 说话中，根据模式配置音频处理
 * 
 * @note 这是应用程序最核心的状态管理函数
 * @see DeviceState
 * @see DeviceStateEventManager
 */
void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) {
        return;
    }
    
    clock_ticks_ = 0;  // 重置时钟计数器，用于屏保计时
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

    // 发送状态变化事件通知
    DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state, state);

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();  // 通知LED状态变化
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);  // 显示待机状态
            display->SetEmotion("neutral");              // 中性表情
            audio_service_.EnableVoiceProcessing(false); // 禁用语音处理
            audio_service_.EnableWakeWordDetection(true); // 启用唤醒词检测
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);  // 显示连接中
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");          // 清空系统消息
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);   // 显示监听中
            display->SetEmotion("neutral");

            // 确保音频处理器正在运行
            if (!audio_service_.IsAudioProcessorRunning()) {
                // 发送开始监听命令
                protocol_->SendStartListening(listening_mode_);
                audio_service_.EnableVoiceProcessing(true);   // 启用语音处理
                audio_service_.EnableWakeWordDetection(false); // 禁用唤醒词检测（避免误触发）
            }
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);    // 显示说话中

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // 仅在AFE唤醒词模式下，说话期间也启用唤醒词检测
#if CONFIG_USE_AFE_WAKE_WORD
                audio_service_.EnableWakeWordDetection(true);
#else
                audio_service_.EnableWakeWordDetection(false);
#endif
            }
            audio_service_.ResetDecoder();  // 重置音频解码器
            break;
        default:
            // 其他状态不做特殊处理
            break;
    }
}

/**
 * @brief 重启设备
 * @details 执行系统软重启，重新加载固件。
 * 
 * 使用场景：
 * - OTA升级完成后
 * - 系统出现严重错误需要恢复
 * - 用户手动请求重启
 * 
 * @note 重启后会重新执行整个启动流程
 * @see Start()
 */
void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
}

/**
 * @brief 外部触发唤醒词
 * @param wake_word 检测到的唤醒词字符串
 * @details 供外部模块（如按键、触摸）调用的唤醒词触发接口。
 * 
 * 根据当前状态执行不同操作：
 * - Idle状态：开始对话并发送唤醒词事件
 * - Speaking状态：中止说话
 * - Listening状态：关闭音频通道
 * 
 * @note 与OnWakeWordDetected不同，此方法由外部事件触发
 * @see OnWakeWordDetected()
 * @see ToggleChatState()
 */
void Application::WakeWordInvoke(const std::string& wake_word) {
    if (device_state_ == kDeviceStateIdle) {
        ToggleChatState();
        Schedule([this, wake_word]() {
            if (protocol_) {
                protocol_->SendWakeWordDetected(wake_word); 
            }
        }); 
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

/**
 * @brief 检查是否可以进入睡眠模式
 * @return true 可以进入睡眠模式
 * @return false 不能进入睡眠模式
 * @details 检查当前系统状态，判断是否满足进入睡眠模式的条件。
 * 
 * 睡眠条件：
 * 1. 设备处于空闲状态(kDeviceStateIdle)
 * 2. 音频通道已关闭
 * 3. 音频服务处于空闲状态
 * 
 * 使用场景：
 * - 电池供电时节省电量
 * - 长时间无操作时自动休眠
 * 
 * @note 进入睡眠前必须确保所有任务已完成
 * @see Board::SetPowerSaveMode()
 */
bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // 现在可以安全地进入睡眠模式
    return true;
}

/**
 * @brief 发送MCP（Model Context Protocol）消息
 * @param payload 消息内容（JSON格式字符串）
 * @details 发送MCP协议消息到服务器，支持主线程和子线程调用。
 * 
 * MCP协议用途：
 * - 工具调用（Tool Calling）
 * - 上下文管理
 * - 扩展功能通信
 * 
 * 线程安全：
 * - 如果在主线程调用，直接发送
 * - 如果在子线程调用，通过Schedule调度到主线程发送
 * 
 * @note MCP是一种开放的AI模型上下文协议标准
 * @see McpServer
 * @see Schedule()
 */
void Application::SendMcpMessage(const std::string& payload) {
    if (protocol_ == nullptr) {
        return;
    }

    // 确保在主线程中发送MCP消息
    if (xTaskGetCurrentTaskHandle() == main_event_loop_task_handle_) {
        ESP_LOGI(TAG, "在主线程中发送MCP消息");
        protocol_->SendMcpMessage(payload);
    } else {
        ESP_LOGI(TAG, "在子线程中发送MCP消息，调度到主线程");
        Schedule([this, payload = std::move(payload)]() {
            protocol_->SendMcpMessage(payload);
        });
    }
}

/**
 * @brief 设置回声消除（AEC）模式
 * @param mode AEC模式（关闭/服务器端/设备端）
 * @details 配置回声消除模式，影响音频处理的回声消除策略。
 * 
 * AEC模式说明：
 * - kAecOff: 关闭回声消除，适合安静环境
 * - kAecOnServerSide: 使用服务器端回声消除，设备端不处理
 * - kAecOnDeviceSide: 使用设备端回声消除，实时处理
 * 
 * 模式切换影响：
 * - 更新音频服务的AEC配置
 * - 显示模式切换通知
 * - 如果音频通道已打开，自动关闭通道（需要重新建立连接）
 * 
 * @note 切换AEC模式会中断当前对话
 * @see AecMode
 * @see AudioService::EnableDeviceAec()
 */
void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // 如果AEC模式改变，关闭音频通道
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

/**
 * @brief 播放提示音
 * @param sound 提示音资源（OGG格式）
 * @details 播放预定义的提示音，用于状态反馈和用户提示。
 * 
 * 常见提示音：
 * - OGG_SUCCESS: 操作成功
 * - OGG_EXCLAMATION: 警告/错误
 * - OGG_UPGRADE: 升级提示
 * - OGG_ACTIVATION: 激活提示
 * - OGG_POPUP: 唤醒提示
 * 
 * @note 提示音资源存储在固件中，使用OGG格式压缩
 * @see Lang::Sounds
 * @see AudioService::PlaySound()
 */
void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}