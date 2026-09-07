/**
 * @file ota.h
 * @brief OTA（Over-The-Air）升级管理头文件
 * 
 * @details
 * 本文件定义了Ota类，用于管理设备的固件空中升级功能。
 * 支持版本检查、固件下载、安全升级、设备激活等功能。
 * 
 * 主要功能：
 * - 固件版本检查：与服务器通信检查是否有新版本
 * - OTA固件升级：下载并烧录新固件
 * - 设备激活：处理设备首次使用的激活流程
 * - 配置获取：从服务器获取MQTT/WebSocket配置
 * - 时间同步：从服务器获取当前时间
 * - 版本验证：标记当前固件为有效（防止回滚）
 * 
 * 安全特性：
 * - 支持序列号读取（从eFuse）
 * - HMAC-SHA256设备激活验证
 * - 固件版本验证（防止降级）
 * - 固件签名验证（ESP-IDF原生支持）
 * 
 * 使用示例：
 * @code
 * Ota ota;
 * 
 * // 检查版本
 * if (ota.CheckVersion()) {
 *     if (ota.HasNewVersion()) {
 *         // 执行升级
 *         bool success = ota.StartUpgrade([](int progress, size_t speed) {
 *             printf("Progress: %d%%, Speed: %zu B/s\n", progress, speed);
 *         });
 *         if (success) {
 *             esp_restart();  // 升级成功，重启设备
 *         }
 *     }
 * }
 * 
 * // 标记当前版本有效
 * ota.MarkCurrentVersionValid();
 * @endcode
 * 
 * @note 升级过程中会停止音频服务，升级失败会自动恢复
 * @see https://ccnphfhqs21z.feishu.cn/wiki/FjW6wZmisimNBBkov6OcmfvknVd
 */

#ifndef _OTA_H
#define _OTA_H

#include <functional>
#include <string>

#include <esp_err.h>
#include "board.h"

/**
 * @brief OTA升级管理类
 * @details 提供完整的OTA升级功能，包括版本检查、固件下载、设备激活等。
 * 
 * 工作流程：
 * 1. 调用CheckVersion()检查服务器是否有新版本
 * 2. 如果有新版本，调用StartUpgrade()执行升级
 * 3. 升级成功后重启设备
 * 4. 新固件启动后调用MarkCurrentVersionValid()标记为有效
 * 
 * 激活流程：
 * 1. 首次使用设备时，CheckVersion()返回激活码
 * 2. 显示激活码给用户
 * 3. 用户在小程序中输入激活码完成绑定
 * 4. 调用Activate()完成设备激活
 */
class Ota {
public:
    /**
     * @brief 构造函数
     * @details 初始化OTA管理器，从eFuse读取设备序列号
     */
    Ota();

    /**
     * @brief 析构函数
     */
    ~Ota();

    /**
     * @brief 检查固件版本
     * @return true表示成功获取版本信息，false表示检查失败
     * @details 与OTA服务器通信，检查是否有新版本固件可用。
     * 同时获取以下信息：
     * - 激活码（首次使用）
     * - MQTT/WebSocket配置
     * - 服务器时间
     * - 固件下载URL
     * 
     * HTTP请求头包含：
     * - Device-Id: 设备MAC地址
     * - Client-Id: 设备UUID
     * - Serial-Number: 设备序列号（如果有）
     * - User-Agent: 设备信息
     */
    bool CheckVersion();

    /**
     * @brief 激活设备
     * @return ESP_OK表示激活成功，ESP_ERR_TIMEOUT表示等待中，其他表示失败
     * @details 向服务器发送激活请求，使用HMAC-SHA256验证设备身份。
     * 需要设备支持HMAC硬件加速（SOC_HMAC_SUPPORTED）。
     * 
     * 激活流程：
     * 1. 从CheckVersion()获取challenge
     * 2. 使用eFuse Key0计算HMAC
     * 3. 发送序列号、challenge和HMAC到服务器
     * 4. 服务器验证后完成激活
     */
    esp_err_t Activate();

    /**
     * @brief 检查是否有激活挑战
     * @return true表示需要激活设备
     */
    bool HasActivationChallenge() { return has_activation_challenge_; }

    /**
     * @brief 检查是否有新版本
     * @return true表示有新版本可用
     */
    bool HasNewVersion() { return has_new_version_; }

    /**
     * @brief 检查是否有MQTT配置
     * @return true表示服务器返回了MQTT配置
     */
    bool HasMqttConfig() { return has_mqtt_config_; }

    /**
     * @brief 检查是否有WebSocket配置
     * @return true表示服务器返回了WebSocket配置
     */
    bool HasWebsocketConfig() { return has_websocket_config_; }

    /**
     * @brief 检查是否有激活码
     * @return true表示需要显示激活码给用户
     */
    bool HasActivationCode() { return has_activation_code_; }

    /**
     * @brief 检查是否获取到服务器时间
     * @return true表示成功同步服务器时间
     */
    bool HasServerTime() { return has_server_time_; }

    /**
     * @brief 开始固件升级
     * @param callback 进度回调函数，参数为进度百分比(0-100)和下载速度(B/s)
     * @return true表示升级成功，false表示升级失败
     * @details 下载并烧录新固件。升级过程中会：
     * 1. 下载固件到OTA分区
     * 2. 验证固件签名和版本
     * 3. 设置启动分区
     * 
     * @note 升级成功后需要调用esp_restart()重启设备
     */
    bool StartUpgrade(std::function<void(int progress, size_t speed)> callback);

    /**
     * @brief 标记当前固件版本为有效
     * @details 新固件首次启动后调用，防止回滚到旧版本。
     * 如果固件运行正常，应在启动后调用此方法。
     * 
     * @note 从factory分区启动时不会执行标记
     */
    void MarkCurrentVersionValid();

    /**
     * @brief 获取新版本固件版本号
     * @return 固件版本号字符串
     */
    const std::string& GetFirmwareVersion() const { return firmware_version_; }

    /**
     * @brief 获取当前运行固件版本号
     * @return 当前版本号字符串
     */
    const std::string& GetCurrentVersion() const { return current_version_; }

    /**
     * @brief 获取激活提示消息
     * @return 激活提示消息字符串
     */
    const std::string& GetActivationMessage() const { return activation_message_; }

    /**
     * @brief 获取激活码
     * @return 激活码字符串（6位数字）
     */
    const std::string& GetActivationCode() const { return activation_code_; }

    /**
     * @brief 获取版本检查URL
     * @return 版本检查URL字符串
     * @details 优先从WiFi设置中读取，如未设置则使用CONFIG_OTA_URL
     */
    std::string GetCheckVersionUrl();

private:
    std::string activation_message_;            ///< 激活提示消息
    std::string activation_code_;               ///< 激活码（6位数字）
    bool has_new_version_ = false;              ///< 是否有新版本
    bool has_mqtt_config_ = false;              ///< 是否有MQTT配置
    bool has_websocket_config_ = false;         ///< 是否有WebSocket配置
    bool has_server_time_ = false;              ///< 是否获取到服务器时间
    bool has_activation_code_ = false;          ///< 是否有激活码
    bool has_serial_number_ = false;            ///< 是否有设备序列号
    bool has_activation_challenge_ = false;     ///< 是否有激活挑战
    std::string current_version_;               ///< 当前固件版本
    std::string firmware_version_;              ///< 新版本固件版本
    std::string firmware_url_;                  ///< 固件下载URL
    std::string activation_challenge_;          ///< 激活挑战字符串
    std::string serial_number_;                 ///< 设备序列号（从eFuse读取）
    int activation_timeout_ms_ = 30000;         ///< 激活超时时间（毫秒）

    /**
     * @brief 执行固件升级
     * @param firmware_url 固件下载URL
     * @return true表示升级成功
     * @details 实际的固件下载和烧录实现
     */
    bool Upgrade(const std::string& firmware_url);

    std::function<void(int progress, size_t speed)> upgrade_callback_;  ///< 升级进度回调

    /**
     * @brief 解析版本号字符串
     * @param version 版本号字符串（如"1.2.3"）
     * @return 版本号数字数组
     */
    std::vector<int> ParseVersion(const std::string& version);

    /**
     * @brief 检查新版本是否可用
     * @param currentVersion 当前版本
     * @param newVersion 新版本
     * @return true表示新版本可用
     * @details 使用语义化版本号比较（如1.2.3 > 1.2.0）
     */
    bool IsNewVersionAvailable(const std::string& currentVersion, const std::string& newVersion);

    /**
     * @brief 获取激活请求负载
     * @return JSON格式的激活请求数据
     * @details 包含序列号、challenge和HMAC签名
     */
    std::string GetActivationPayload();

    /**
     * @brief 设置HTTP客户端
     * @return 配置好的HTTP客户端
     * @details 配置HTTP请求头，包括设备标识、序列号等
     */
    std::unique_ptr<Http> SetupHttp();
};

#endif // _OTA_H
