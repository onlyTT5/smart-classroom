/**
 * @file dht11_sensor.cc
 * @brief DHT11温湿度传感器实现文件，提供虚拟温湿度数据生成功能
 * 
 * 本文件实现了Dht11Sensor类，用于模拟DHT11传感器的数据读取。
 * 该类提供虚拟温湿度数据生成功能，用于在没有实际硬件的情况下测试系统。
 * 
 * @author 粤嵌.温工
 * @date 2026-02-24
 * @version 1.0.0
 * 
 * @section 实现说明
 * 
 * 1. 虚拟数据生成：
 *    - 使用rand()函数生成随机数
 *    - 温度值范围：20.0℃ - 30.0℃（初始生成）
 *    - 湿度值范围：40.0% - 80.0%（初始生成）
 *    - 数据保留一位小数
 * 
 * 2. 设备唤醒时数据调整：
 *    - 温度值：在当前值基础上增加0.5℃
 *    - 温度值上限：40.0℃，超过后重置为20.0℃
 *    - 湿度值：重新生成40.0% - 80.0%之间的随机值
 * 
 * 3. 数据访问：
 *    - 温湿度数据存储在成员变量中
 *    - 提供多种访问方式（单独获取、同时获取、字符串格式）
 *    - 所有访问方法都检查初始化状态
 * 
 * 
 * @section 性能说明
 * 
 * - 数据生成：使用随机数生成，模拟真实传感器数据
 * - 数据访问：内存中存储，访问速度快
 * - 响应时间：微秒级
 * - 内存占用：约8字节（两个float成员变量）
 * 
 * @section 调试说明
 * 
 * - 使用ESP_LOGI()输出操作日志
 * - 使用ESP_LOGE()输出错误日志
 * - 使用ESP_LOGW()输出警告日志
 * - 日志标签为"DHT11_SENSOR"
 * - 可通过menuconfig调整日志级别
 */

#include "dht11_sensor.h"
#include <esp_log.h>
#include <sstream>
#include <iomanip>
#include <cstdlib>

/**
 * @brief 日志标签，用于标识DHT11传感器的日志输出
 * 
 * 该标签用于所有DHT11传感器相关的日志输出
 * 可通过menuconfig调整日志级别
 * 
 * @note 日志标签应简短且具有描述性
 * @note 建议使用大写字母和下划线
 */
static const char* TAG = "DHT11_SENSOR";

/**
 * @brief 构造函数
 * 
 * 初始化DHT11传感器的成员变量
 * 将初始化状态设置为false
 * 设置初始温度值为25.0℃，湿度值为60.0%
 * 
 * @note 构造函数不会自动初始化传感器，需要显式调用Initialize()方法
 * @note 构造函数是线程安全的
 * 
 * @see Initialize()
 */
Dht11Sensor::Dht11Sensor() : temperature_(25.0f), humidity_(60.0f), initialized_(false) {
    ESP_LOGI(TAG, "DHT11传感器构造函数调用");
}

/**
 * @brief 析构函数
 * 
 * 清理DHT11传感器资源
 * 
 * @note 析构函数是线程安全的
 */
Dht11Sensor::~Dht11Sensor() {
    ESP_LOGI(TAG, "DHT11传感器析构函数调用");
}

/**
 * @brief 初始化传感器
 * 
 * 初始化虚拟温湿度数据生成机制
 * 初始化过程包括：
 * 1. 检查是否已经初始化，避免重复初始化
 * 2. 生成初始虚拟温度数据
 * 3. 生成初始虚拟湿度数据
 * 4. 更新初始化状态
 * 
 * @return bool 初始化成功返回true，失败返回false
 * 
 * @note 如果已经初始化，会返回true并输出警告日志
 * @note 初始化成功后会输出温度和湿度值
 * @note 建议在系统启动时尽早调用此方法
 * 
 * @warning 不要在多线程环境中同时调用此方法
 * 
 * @see GetTemperature()
 * @see GetHumidity()
 * @see GetTemperatureAndHumidity()
 */
bool Dht11Sensor::Initialize() {
    if (initialized_) {
        ESP_LOGW(TAG, "DHT11传感器已经初始化，跳过重复初始化");
        return true;
    }

    ESP_LOGI(TAG, "开始初始化DHT11传感器");

    // 生成初始虚拟温湿度数据
    temperature_ = GenerateVirtualTemperature();
    humidity_ = GenerateVirtualHumidity();

    // 更新初始化状态
    initialized_ = true;
    ESP_LOGI(TAG, "DHT11传感器初始化成功");
    ESP_LOGI(TAG, "初始温度: %.1f℃, 湿度: %.1f%%", temperature_, humidity_);
    return true;
}

/**
 * @brief 生成虚拟温度值
 * 
 * 生成20.0℃ - 30.0℃之间的随机温度值
 * 用于模拟DHT11传感器的温度数据
 * 
 * @return float 虚拟温度值（20.0℃ - 30.0℃）
 * 
 * @note 使用rand()函数生成随机数
 * @note 温度值保留一位小数
 * @note 温度值范围：20.0℃ - 30.0℃
 * 
 * @see Initialize()
 * @see GetTemperature()
 */
float Dht11Sensor::GenerateVirtualTemperature() {
    // 生成20.0℃ - 30.0℃之间的随机温度值
    // rand() % 100 生成0-99之间的随机数
    // 除以10.0得到0.0-9.9之间的随机数
    // 加上20.0得到20.0-29.9之间的随机数
    float temp = 20.0f + static_cast<float>(rand() % 100) / 10.0f;
    ESP_LOGI(TAG, "生成虚拟温度值: %.1f℃", temp);
    return temp;
}

/**
 * @brief 生成虚拟湿度值
 * 
 * 生成40.0% - 80.0%之间的随机湿度值
 * 用于模拟DHT11传感器的湿度数据
 * 
 * @return float 虚拟湿度值（40.0% - 80.0%）
 * 
 * @note 使用rand()函数生成随机数
 * @note 湿度值保留一位小数
 * @note 湿度值范围：40.0% - 80.0%
 * 
 * @see Initialize()
 * @see GetHumidity()
 */
float Dht11Sensor::GenerateVirtualHumidity() {
    // 生成40.0% - 80.0%之间的随机湿度值
    // rand() % 400 生成0-399之间的随机数
    // 除以10.0得到0.0-39.9之间的随机数
    // 加上40.0得到40.0-79.9之间的随机数
    float hum = 40.0f + static_cast<float>(rand() % 400) / 10.0f;
    ESP_LOGI(TAG, "生成虚拟湿度值: %.1f%%", hum);
    return hum;
}

/**
 * @brief 获取温度值
 * 
 * 返回当前温度值（摄氏度）
 * 
 * @return float 当前温度值（摄氏度）
 * 
 * @note 如果传感器未初始化，会返回0.0f并输出错误日志
 * @note 温度值保留一位小数
 * @note 温度值范围：20.0℃ - 40.0℃（考虑唤醒调整）
 * 
 * @warning 确保传感器已初始化
 * 
 * @see GetHumidity()
 * @see GetTemperatureAndHumidity()
 */
float Dht11Sensor::GetTemperature() {
    if (!initialized_) {
        ESP_LOGE(TAG, "DHT11传感器未初始化");
        return 0.0f;
    }

    ESP_LOGI(TAG, "获取温度: %.1f℃", temperature_);
    return temperature_;
}

/**
 * @brief 获取湿度值
 * 
 * 返回当前湿度值（百分比）
 * 
 * @return float 当前湿度值（百分比）
 * 
 * @note 如果传感器未初始化，会返回0.0f并输出错误日志
 * @note 湿度值保留一位小数
 * @note 湿度值范围：40.0% - 80.0%
 * 
 * @warning 确保传感器已初始化
 * 
 * @see GetTemperature()
 * @see GetTemperatureAndHumidity()
 */
float Dht11Sensor::GetHumidity() {
    if (!initialized_) {
        ESP_LOGE(TAG, "DHT11传感器未初始化");
        return 0.0f;
    }

    ESP_LOGI(TAG, "获取湿度: %.1f%%", humidity_);
    return humidity_;
}

/**
 * @brief 获取温湿度值
 * 
 * 同时获取温度和湿度值
 * 
 * @param temperature 输出参数，温度值（摄氏度）
 * @param humidity 输出参数，湿度值（百分比）
 * @return bool 获取成功返回true，失败返回false
 * 
 * @note 如果传感器未初始化，会返回false并输出错误日志
 * @note 温度值保留一位小数
 * @note 湿度值保留一位小数
 * 
 * @warning 确保传感器已初始化
 * 
 * @see GetTemperature()
 * @see GetHumidity()
 */
bool Dht11Sensor::GetTemperatureAndHumidity(float& temperature, float& humidity) {
    if (!initialized_) {
        ESP_LOGE(TAG, "DHT11传感器未初始化");
        return false;
    }

    temperature = temperature_;
    humidity = humidity_;
    ESP_LOGI(TAG, "获取温湿度: %.1f℃, %.1f%%", temperature, humidity);
    return true;
}

/**
 * @brief 设备唤醒时调用，动态调整温度值
 * 
 * 每次设备被唤醒时，温度值额外增加0.5℃
 * 温度值上限为40.0℃，超过后重置为20.0℃
 * 湿度值重新生成，保持数据的动态性
 * 
 * @note 如果传感器未初始化，会输出错误日志并直接返回
 * @note 温度值增加0.5℃后，如果超过40.0℃，重置为20.0℃
 * @note 湿度值重新生成，范围：40.0% - 80.0%
 * @note 调整成功后会输出新的温度和湿度值
 * 
 * @warning 确保传感器已初始化
 * 
 * @see Initialize()
 * @see GetTemperature()
 */
void Dht11Sensor::OnWakeUp() {
    if (!initialized_) {
        ESP_LOGE(TAG, "DHT11传感器未初始化");
        return;
    }

    ESP_LOGI(TAG, "设备唤醒，调整温湿度数据");

    // 温度值额外增加0.5℃
    temperature_ += 0.5f;

    // 温度值上限为40.0℃，超过后重置为20.0℃
    if (temperature_ > 40.0f) {
        temperature_ = 20.0f;
        ESP_LOGI(TAG, "温度超过上限，重置为20.0℃");
    }

    // 重新生成湿度值，保持数据的动态性
    humidity_ = GenerateVirtualHumidity();

    ESP_LOGI(TAG, "设备唤醒后，温度: %.1f℃, 湿度: %.1f%%", temperature_, humidity_);
}

/**
 * @brief 获取温度值的字符串表示
 * 
 * 返回温度值的字符串表示，包含单位
 * 
 * @return std::string 温度值的字符串表示（如"25.5℃"）
 * 
 * @note 如果传感器未初始化，会返回"传感器未初始化"
 * @note 温度值保留一位小数
 * @note 字符串格式："XX.X℃"
 * 
 * @warning 确保传感器已初始化
 * 
 * @see GetHumidityString()
 * @see GetTemperatureAndHumidityString()
 */
std::string Dht11Sensor::GetTemperatureString() {
    if (!initialized_) {
        ESP_LOGE(TAG, "DHT11传感器未初始化");
        return "传感器未初始化";
    }

    std::ostringstream oss;
    // 设置浮点数格式，保留一位小数
    oss << std::fixed << std::setprecision(1) << temperature_ << "℃";
    std::string result = oss.str();
    ESP_LOGI(TAG, "获取温度字符串: %s", result.c_str());
    return result;
}

/**
 * @brief 获取湿度值的字符串表示
 * 
 * 返回湿度值的字符串表示，包含单位
 * 
 * @return std::string 湿度值的字符串表示（如"65.0%"）
 * 
 * @note 如果传感器未初始化，会返回"传感器未初始化"
 * @note 湿度值保留一位小数
 * @note 字符串格式："XX.X%"
 * 
 * @warning 确保传感器已初始化
 * 
 * @see GetTemperatureString()
 * @see GetTemperatureAndHumidityString()
 */
std::string Dht11Sensor::GetHumidityString() {
    if (!initialized_) {
        ESP_LOGE(TAG, "DHT11传感器未初始化");
        return "传感器未初始化";
    }

    std::ostringstream oss;
    // 设置浮点数格式，保留一位小数
    oss << std::fixed << std::setprecision(1) << humidity_ << "%";
    std::string result = oss.str();
    ESP_LOGI(TAG, "获取湿度字符串: %s", result.c_str());
    return result;
}

/**
 * @brief 获取温湿度值的字符串表示
 * 
 * 返回温湿度值的字符串表示，包含单位和标签
 * 
 * @return std::string 温湿度值的字符串表示（如"温度：25.5℃，湿度：65.0%"）
 * 
 * @note 如果传感器未初始化，会返回"传感器未初始化"
 * @note 温度值保留一位小数
 * @note 湿度值保留一位小数
 * @note 字符串格式："温度：XX.X℃，湿度：XX.X%"
 * 
 * @warning 确保传感器已初始化
 * 
 * @see GetTemperatureString()
 * @see GetHumidityString()
 */
std::string Dht11Sensor::GetTemperatureAndHumidityString() {
    if (!initialized_) {
        ESP_LOGE(TAG, "DHT11传感器未初始化");
        return "传感器未初始化";
    }

    std::ostringstream oss;
    // 设置浮点数格式，保留一位小数
    oss << "温度：" << std::fixed << std::setprecision(1) << temperature_ << "℃，湿度：" << humidity_ << "%";
    std::string result = oss.str();
    ESP_LOGI(TAG, "获取温湿度字符串: %s", result.c_str());
    return result;
}
