/**
 * @file ota.cc
 * @brief OTA（Over-The-Air）升级管理实现文件
 * 
 * @details
 * 本文件实现了Ota类的所有功能，包括：
 * - 固件版本检查与服务器通信
 * - OTA固件下载和烧录
 * - 设备激活流程（HMAC-SHA256验证）
 * - 配置获取（MQTT/WebSocket/时间同步）
 * 
 * 服务器通信协议：
 * 版本检查URL: 从WiFi设置或CONFIG_OTA_URL获取
 * 请求方法: POST（有设备信息）或 GET（无设备信息）
 * 请求头:
 *   - Activation-Version: 1或2（是否有序列号）
 *   - Device-Id: 设备MAC地址
 *   - Client-Id: 设备UUID
 *   - Serial-Number: 设备序列号（如果有）
 *   - User-Agent: 设备信息
 *   - Accept-Language: 语言代码
 *   - Content-Type: application/json
 * 
 * 响应JSON格式:
 * @code
 * {
 *   "activation": {
 *     "message": "请使用小程序绑定设备",
 *     "code": "123456",
 *     "challenge": "随机字符串",
 *     "timeout_ms": 30000
 *   },
 *   "mqtt": {
 *     "broker": "mqtt.example.com",
 *     "port": 1883,
 *     "username": "user",
 *     "password": "pass"
 *   },
 *   "websocket": {
 *     "url": "wss://example.com/ws"
 *   },
 *   "server_time": {
 *     "timestamp": 1234567890000,
 *     "timezone_offset": 480
 *   },
 *   "firmware": {
 *     "version": "1.2.3",
 *     "url": "http://example.com/firmware.bin",
 *     "force": 0
 *   }
 * }
 * @endcode
 * 
 * 激活流程：
 * 1. 首次使用设备，CheckVersion()返回activation.code
 * 2. 显示激活码给用户，用户在小程序中输入
 * 3. 调用Activate()，使用HMAC-SHA256验证设备
 * 4. 服务器验证通过后，设备完成激活
 * 
 * OTA升级流程：
 * 1. CheckVersion()检查新版本
 * 2. StartUpgrade()下载固件到OTA分区
 * 3. 验证固件签名和版本
 * 4. 设置启动分区为新固件
 * 5. esp_restart()重启设备
 * 6. 新固件启动后调用MarkCurrentVersionValid()
 * 
 * @note 详细协议规范请参考飞书文档
 * @see https://ccnphfhqs21z.feishu.cn/wiki/FjW6wZmisimNBBkov6OcmfvknVd
 */

#include "ota.h"
#include "system_info.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#ifdef SOC_HMAC_SUPPORTED
#include <esp_hmac.h>
#endif

#include <cstring>
#include <vector>
#include <sstream>
#include <algorithm>

#define TAG "Ota"


/**
 * @brief 构造函数
 * @details 初始化OTA管理器，从eFuse USER_DATA块读取设备序列号。
 * 序列号用于设备激活和身份验证。
 * 
 * eFuse读取：
 * - 块: ESP_EFUSE_BLOCK_USR_DATA
 * - 大小: 32字节（256位）
 * - 格式: 字符串，以null结尾
 * 
 * @note 如果eFuse未烧录序列号，has_serial_number_为false
 */
Ota::Ota() {
#ifdef ESP_EFUSE_BLOCK_USR_DATA
    // 从eFuse USER_DATA块读取序列号
    uint8_t serial_number[33] = {0};  // 32字节数据 + 1字节null终止符
    if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, serial_number, 32 * 8) == ESP_OK) {
        if (serial_number[0] == 0) {
            // 序列号未烧录
            has_serial_number_ = false;
        } else {
            // 成功读取序列号
            serial_number_ = std::string(reinterpret_cast<char*>(serial_number), 32);
            has_serial_number_ = true;
        }
    }
#endif
}

/**
 * @brief 析构函数
 */
Ota::~Ota() {
}

/**
 * @brief 获取版本检查URL
 * @return 版本检查URL字符串
 * @details 获取用于检查固件版本的URL，优先级：
 * 1. WiFi设置中的"ota_url"配置项
 * 2. CONFIG_OTA_URL编译配置
 * 
 * @note 如果URL长度小于10，会被视为无效配置
 */
std::string Ota::GetCheckVersionUrl() {
    // 从WiFi设置中读取OTA URL
    Settings settings("wifi", false);
    std::string url = settings.GetString("ota_url");
    // 如果未设置，使用编译时配置的URL
    if (url.empty()) {
        url = CONFIG_OTA_URL;
    }
    return url;
}

/**
 * @brief 设置HTTP客户端
 * @return 配置好的HTTP客户端
 * @details 创建并配置HTTP客户端，设置必要的请求头：
 * - Activation-Version: 1或2（是否有序列号）
 * - Device-Id: 设备MAC地址
 * - Client-Id: 设备UUID
 * - Serial-Number: 设备序列号（如果有）
 * - User-Agent: 设备信息
 * - Accept-Language: 语言代码
 * - Content-Type: application/json
 */
std::unique_ptr<Http> Ota::SetupHttp() {
    auto& board = Board::GetInstance();
    auto network = board.GetNetwork();
    auto http = network->CreateHttp(0);
    auto user_agent = SystemInfo::GetUserAgent();

    // 设置激活版本（1=无序列号，2=有序列号）
    http->SetHeader("Activation-Version", has_serial_number_ ? "2" : "1");
    // 设置设备标识
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", board.GetUuid());

    // 如果有序列号，添加到请求头
    if (has_serial_number_) {
        http->SetHeader("Serial-Number", serial_number_.c_str());
        ESP_LOGI(TAG, "Setup HTTP, User-Agent: %s, Serial-Number: %s", user_agent.c_str(), serial_number_.c_str());
    }

    // 设置其他请求头
    http->SetHeader("User-Agent", user_agent);
    http->SetHeader("Accept-Language", Lang::CODE);
    http->SetHeader("Content-Type", "application/json");

    return http;
}

/**
 * @brief 检查固件版本
 * @return true表示成功获取版本信息
 * @details 与OTA服务器通信，检查固件更新并获取配置信息。
 * 
 * 请求流程：
 * 1. 获取当前固件版本
 * 2. 构建HTTP请求（POST或GET）
 * 3. 发送请求到OTA服务器
 * 4. 解析JSON响应
 * 
 * 响应解析：
 * - activation: 设备激活信息（首次使用）
 * - mqtt: MQTT服务器配置
 * - websocket: WebSocket服务器配置
 * - server_time: 服务器时间（用于时间同步）
 * - firmware: 新固件信息（版本号、URL、强制升级标志）
 * 
 */
bool Ota::CheckVersion() {
    auto& board = Board::GetInstance();
    auto app_desc = esp_app_get_description();

    // 获取当前固件版本
    current_version_ = app_desc->version;
    ESP_LOGI(TAG, "Current version: %s", current_version_.c_str());

    // 获取版本检查URL
    std::string url = GetCheckVersionUrl();
    if (url.length() < 10) {
        ESP_LOGE(TAG, "Check version URL is not properly set");
        return false;
    }

    // 设置HTTP客户端
    auto http = SetupHttp();

    // 准备请求数据（设备信息JSON）
    std::string data = board.GetJson();
    std::string method = data.length() > 0 ? "POST" : "GET";
    http->SetContent(std::move(data));

    // 发送HTTP请求
    if (!http->Open(method, url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        return false;
    }

    // 检查HTTP响应状态
    auto status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "Failed to check version, status code: %d", status_code);
        return false;
    }

    // 读取响应数据
    data = http->ReadAll();
    http->Close();

    // 解析JSON响应
    cJSON *root = cJSON_Parse(data.c_str());
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return false;
    }

    // 解析激活信息
    has_activation_code_ = false;
    has_activation_challenge_ = false;
    cJSON *activation = cJSON_GetObjectItem(root, "activation");
    if (cJSON_IsObject(activation)) {
        // 获取激活提示消息
        cJSON* message = cJSON_GetObjectItem(activation, "message");
        if (cJSON_IsString(message)) {
            activation_message_ = message->valuestring;
        }
        // 获取激活码
        cJSON* code = cJSON_GetObjectItem(activation, "code");
        if (cJSON_IsString(code)) {
            activation_code_ = code->valuestring;
            has_activation_code_ = true;
        }
        // 获取激活挑战
        cJSON* challenge = cJSON_GetObjectItem(activation, "challenge");
        if (cJSON_IsString(challenge)) {
            activation_challenge_ = challenge->valuestring;
            has_activation_challenge_ = true;
        }
        // 获取激活超时时间
        cJSON* timeout_ms = cJSON_GetObjectItem(activation, "timeout_ms");
        if (cJSON_IsNumber(timeout_ms)) {
            activation_timeout_ms_ = timeout_ms->valueint;
        }
    }

    // 解析MQTT配置
    has_mqtt_config_ = false;
    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
    if (cJSON_IsObject(mqtt)) {
        Settings settings("mqtt", true);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, mqtt) {
            if (cJSON_IsString(item)) {
                if (settings.GetString(item->string) != item->valuestring) {
                    settings.SetString(item->string, item->valuestring);
                }
            } else if (cJSON_IsNumber(item)) {
                if (settings.GetInt(item->string) != item->valueint) {
                    settings.SetInt(item->string, item->valueint);
                }
            }
        }
        has_mqtt_config_ = true;
    } else {
        ESP_LOGI(TAG, "No mqtt section found !");
    }

    // 解析WebSocket配置
    has_websocket_config_ = false;
    cJSON *websocket = cJSON_GetObjectItem(root, "websocket");
    if (cJSON_IsObject(websocket)) {
        Settings settings("websocket", true);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, websocket) {
            if (cJSON_IsString(item)) {
                if (settings.GetString(item->string) != item->valuestring) {
                    settings.SetString(item->string, item->valuestring);
                }
            } else if (cJSON_IsNumber(item)) {
                if (settings.GetInt(item->string) != item->valueint) {
                    settings.SetInt(item->string, item->valueint);
                }
            }
        }
        has_websocket_config_ = true;
    } else {
        ESP_LOGI(TAG, "No websocket section found!");
    }

    // 解析服务器时间
    has_server_time_ = false;
    cJSON *server_time = cJSON_GetObjectItem(root, "server_time");
    if (cJSON_IsObject(server_time)) {
        cJSON *timestamp = cJSON_GetObjectItem(server_time, "timestamp");
        cJSON *timezone_offset = cJSON_GetObjectItem(server_time, "timezone_offset");
        
        if (cJSON_IsNumber(timestamp)) {
            // 设置系统时间
            struct timeval tv;
            double ts = timestamp->valuedouble;
            
            // 如果有时区偏移，计算本地时间
            if (cJSON_IsNumber(timezone_offset)) {
                ts += (timezone_offset->valueint * 60 * 1000); // 转换分钟为毫秒
            }
            
            tv.tv_sec = (time_t)(ts / 1000);  // 转换毫秒为秒
            tv.tv_usec = (suseconds_t)((long long)ts % 1000) * 1000;  // 剩余的毫秒转换为微秒
            settimeofday(&tv, NULL);
            has_server_time_ = true;
        }
    } else {
        ESP_LOGW(TAG, "No server_time section found!");
    }

    // 解析固件信息
    has_new_version_ = false;
    cJSON *firmware = cJSON_GetObjectItem(root, "firmware");
    if (cJSON_IsObject(firmware)) {
        cJSON *version = cJSON_GetObjectItem(firmware, "version");
        if (cJSON_IsString(version)) {
            firmware_version_ = version->valuestring;
        }
        cJSON *url = cJSON_GetObjectItem(firmware, "url");
        if (cJSON_IsString(url)) {
            firmware_url_ = url->valuestring;
        }

        if (cJSON_IsString(version) && cJSON_IsString(url)) {
            // 检查版本是否更新
            has_new_version_ = IsNewVersionAvailable(current_version_, firmware_version_);
            if (has_new_version_) {
                ESP_LOGI(TAG, "New version available: %s", firmware_version_.c_str());
            } else {
                ESP_LOGI(TAG, "Current is the latest version");
            }
            // 如果设置了强制升级标志，强制升级
            cJSON *force = cJSON_GetObjectItem(firmware, "force");
            if (cJSON_IsNumber(force) && force->valueint == 1) {
                has_new_version_ = true;
            }
        }
    } else {
        ESP_LOGW(TAG, "No firmware section found!");
    }

    cJSON_Delete(root);
    return true;
}

/**
 * @brief 标记当前固件版本为有效
 * @details 新固件首次启动后调用，防止回滚到旧版本。
 * 
 * 工作原理：
 * 1. 获取当前运行的分区
 * 2. 检查分区状态
 * 3. 如果状态为PENDING_VERIFY，标记为VALID
 * 
 * 分区状态：
 * - ESP_OTA_IMG_NEW: 新固件，未启动过
 * - ESP_OTA_IMG_PENDING_VERIFY: 待验证（首次启动）
 * - ESP_OTA_IMG_VALID: 验证通过
 * - ESP_OTA_IMG_INVALID: 验证失败（会回滚）
 * 
 * @note 从factory分区启动时不会执行标记
 * @note 应在固件运行正常后调用此方法
 */
void Ota::MarkCurrentVersionValid() {
    // 获取当前运行的分区
    auto partition = esp_ota_get_running_partition();
    // 从factory分区启动时不标记
    if (strcmp(partition->label, "factory") == 0) {
        ESP_LOGI(TAG, "Running from factory partition, skipping");
        return;
    }

    ESP_LOGI(TAG, "Running partition: %s", partition->label);
    // 获取分区状态
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(partition, &state) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get state of partition");
        return;
    }

    // 如果状态为待验证，标记为有效
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "Marking firmware as valid");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

/**
 * @brief 执行固件升级
 * @param firmware_url 固件下载URL
 * @return true表示升级成功
 * @details 下载固件并烧录到OTA分区。升级流程：
 * 
 * 1. 获取OTA更新分区
 * 2. 下载固件并验证头部信息
 * 3. 写入固件数据到OTA分区
 * 4. 验证固件完整性
 * 5. 设置启动分区
 * 
 * 进度报告：
 * - 每秒计算一次下载速度和进度
 * - 通过回调函数报告给上层
 * 
 * 安全验证：
 * - 检查固件版本是否与当前相同（防止重复升级）
 * - 验证固件签名（ESP-IDF原生支持）
 * 
 * @note 升级成功后需要调用esp_restart()重启设备
 * @note 升级失败会自动清理，不影响当前运行
 */
bool Ota::Upgrade(const std::string& firmware_url) {
    ESP_LOGI(TAG, "Upgrading firmware from %s", firmware_url.c_str());

    esp_ota_handle_t update_handle = 0;
    // 获取OTA更新分区
    auto update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Failed to get update partition");
        return false;
    }

    ESP_LOGI(TAG, "Writing to partition %s at offset 0x%lx", update_partition->label, update_partition->address);
    bool image_header_checked = false;
    std::string image_header;

    // 创建HTTP客户端并下载固件
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    if (!http->Open("GET", firmware_url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        return false;
    }

    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "Failed to get firmware, status code: %d", http->GetStatusCode());
        return false;
    }

    // 获取固件大小
    size_t content_length = http->GetBodyLength();
    if (content_length == 0) {
        ESP_LOGE(TAG, "Failed to get content length");
        return false;
    }

    // 下载并烧录固件
    char buffer[512];
    size_t total_read = 0, recent_read = 0;
    auto last_calc_time = esp_timer_get_time();
    while (true) {
        int ret = http->Read(buffer, sizeof(buffer));
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to read HTTP data: %s", esp_err_to_name(ret));
            return false;
        }

        // 计算下载速度和进度（每秒一次）
        recent_read += ret;
        total_read += ret;
        if (esp_timer_get_time() - last_calc_time >= 1000000 || ret == 0) {
            size_t progress = total_read * 100 / content_length;
            ESP_LOGI(TAG, "Progress: %u%% (%u/%u), Speed: %uB/s", progress, total_read, content_length, recent_read);
            if (upgrade_callback_) {
                upgrade_callback_(progress, recent_read);
            }
            last_calc_time = esp_timer_get_time();
            recent_read = 0;
        }

        if (ret == 0) {
            break;
        }

        // 检查固件头部信息
        if (!image_header_checked) {
            image_header.append(buffer, ret);
            // 头部包含：镜像头 + 段头 + 应用描述
            if (image_header.size() >= sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                esp_app_desc_t new_app_info;
                memcpy(&new_app_info, image_header.data() + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(esp_app_desc_t));
                ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

                // 检查版本是否与当前相同
                auto current_version = esp_app_get_description()->version;
                if (memcmp(new_app_info.version, current_version, sizeof(new_app_info.version)) == 0) {
                    ESP_LOGE(TAG, "Firmware version is the same, skipping upgrade");
                    return false;
                }

                // 开始OTA写入
                if (esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle)) {
                    esp_ota_abort(update_handle);
                    ESP_LOGE(TAG, "Failed to begin OTA");
                    return false;
                }

                image_header_checked = true;
                std::string().swap(image_header);
            }
        }

        // 写入固件数据
        auto err = esp_ota_write(update_handle, buffer, ret);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write OTA data: %s", esp_err_to_name(err));
            esp_ota_abort(update_handle);
            return false;
        }
    }
    http->Close();

    // 完成OTA写入并验证
    esp_err_t err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "Failed to end OTA: %s", esp_err_to_name(err));
        }
        return false;
    }

    // 设置启动分区
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Firmware upgrade successful");
    return true;
}

/**
 * @brief 开始固件升级
 * @param callback 进度回调函数
 * @return true表示升级成功
 * @details 设置回调函数并开始升级流程。回调函数接收：
 * - progress: 下载进度百分比（0-100）
 * - speed: 下载速度（字节/秒）
 */
bool Ota::StartUpgrade(std::function<void(int progress, size_t speed)> callback) {
    upgrade_callback_ = callback;
    return Upgrade(firmware_url_);
}

/**
 * @brief 解析版本号字符串
 * @param version 版本号字符串（如"1.2.3"）
 * @return 版本号数字数组
 * @details 将点分版本号字符串解析为整数数组，用于版本比较。
 * 例如："1.2.3" -> [1, 2, 3]
 */
std::vector<int> Ota::ParseVersion(const std::string& version) {
    std::vector<int> versionNumbers;
    std::stringstream ss(version);
    std::string segment;
    
    // 按'.'分割版本号
    while (std::getline(ss, segment, '.')) {
        versionNumbers.push_back(std::stoi(segment));
    }
    
    return versionNumbers;
}

/**
 * @brief 检查新版本是否可用
 * @param currentVersion 当前版本
 * @param newVersion 新版本
 * @return true表示新版本可用
 * @details 使用语义化版本号比较规则：
 * - 从左到右逐位比较版本号
 * - 高位版本号不同则直接决定结果
 * - 如果前面都相同，版本号位数更多的为新版本
 * 
 * 示例：
 * - 1.2.3 > 1.2.0
 * - 1.3.0 > 1.2.9
 * - 2.0.0 > 1.9.9
 * - 1.2.3.4 > 1.2.3
 */
bool Ota::IsNewVersionAvailable(const std::string& currentVersion, const std::string& newVersion) {
    std::vector<int> current = ParseVersion(currentVersion);
    std::vector<int> newer = ParseVersion(newVersion);
    
    // 逐位比较版本号
    for (size_t i = 0; i < std::min(current.size(), newer.size()); ++i) {
        if (newer[i] > current[i]) {
            return true;
        } else if (newer[i] < current[i]) {
            return false;
        }
    }
    
    // 如果前面都相同，版本号位数更多的为新版本
    return newer.size() > current.size();
}

/**
 * @brief 获取激活请求负载
 * @return JSON格式的激活请求数据
 * @details 构建设备激活请求的JSON数据，包含：
 * - algorithm: 签名算法（hmac-sha256）
 * - serial_number: 设备序列号
 * - challenge: 服务器返回的挑战字符串
 * - hmac: HMAC-SHA256签名结果
 * 
 * HMAC计算：
 * - 使用eFuse Key0作为密钥
 * - 对challenge进行HMAC-SHA256计算
 * - 结果转换为十六进制字符串
 * 
 * @note 需要设备支持HMAC硬件加速（SOC_HMAC_SUPPORTED）
 * @note 如果设备没有序列号，返回空JSON对象
 */
std::string Ota::GetActivationPayload() {
    // 没有序列号时返回空对象
    if (!has_serial_number_) {
        return "{}";
    }

    std::string hmac_hex;
#ifdef SOC_HMAC_SUPPORTED
    uint8_t hmac_result[32];  // SHA-256输出32字节
    
    // 使用eFuse Key0计算HMAC-SHA256
    esp_err_t ret = esp_hmac_calculate(HMAC_KEY0, (uint8_t*)activation_challenge_.data(), activation_challenge_.size(), hmac_result);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HMAC calculation failed: %s", esp_err_to_name(ret));
        return "{}";
    }

    // 将HMAC结果转换为十六进制字符串
    for (size_t i = 0; i < sizeof(hmac_result); i++) {
        char buffer[3];
        sprintf(buffer, "%02x", hmac_result[i]);
        hmac_hex += buffer;
    }
#endif

    // 构建JSON请求体
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "algorithm", "hmac-sha256");
    cJSON_AddStringToObject(payload, "serial_number", serial_number_.c_str());
    cJSON_AddStringToObject(payload, "challenge", activation_challenge_.c_str());
    cJSON_AddStringToObject(payload, "hmac", hmac_hex.c_str());
    auto json_str = cJSON_PrintUnformatted(payload);
    std::string json(json_str);
    cJSON_free(json_str);
    cJSON_Delete(payload);

    ESP_LOGI(TAG, "Activation payload: %s", json.c_str());
    return json;
}

/**
 * @brief 激活设备
 * @return ESP_OK表示激活成功，ESP_ERR_TIMEOUT表示等待中，其他表示失败
 * @details 向服务器发送激活请求，完成设备绑定流程。
 * 
 * 激活流程：
 * 1. 检查是否有激活挑战
 * 2. 构建激活URL（OTA URL + "/activate"）
 * 3. 发送POST请求，包含HMAC签名
 * 4. 处理服务器响应
 * 
 * 响应状态码：
 * - 200: 激活成功
 * - 202: 等待用户确认（需要重试）
 * - 其他: 激活失败
 * 
 * @note 激活成功后，设备可以正常使用MQTT/WebSocket服务
 * @note 激活失败时，设备会显示激活码等待用户绑定
 */
esp_err_t Ota::Activate() {
    // 检查是否有激活挑战
    if (!has_activation_challenge_) {
        ESP_LOGW(TAG, "No activation challenge found");
        return ESP_FAIL;
    }

    // 构建激活URL
    std::string url = GetCheckVersionUrl();
    if (url.back() != '/') {
        url += "/activate";
    } else {
        url += "activate";
    }

    // 设置HTTP客户端
    auto http = SetupHttp();

    // 准备请求数据
    std::string data = GetActivationPayload();
    http->SetContent(std::move(data));

    // 发送激活请求
    if (!http->Open("POST", url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        return ESP_FAIL;
    }
    
    // 检查响应状态
    auto status_code = http->GetStatusCode();
    if (status_code == 202) {
        // 202表示等待用户确认，需要重试
        return ESP_ERR_TIMEOUT;
    }
    if (status_code != 200) {
        ESP_LOGE(TAG, "Failed to activate, code: %d, body: %s", status_code, http->ReadAll().c_str());
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Activation successful");
    return ESP_OK;
}
