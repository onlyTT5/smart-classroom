/**
 * @file dht11_sensor.h
 * @brief DHT11温湿度传感器头文件，提供虚拟温湿度数据生成功能
 * 
 * 本文件定义了Dht11Sensor类，用于模拟DHT11传感器的数据读取。
 * 该类提供虚拟温湿度数据生成功能，用于在没有实际硬件的情况下测试系统。
 * 
 * @author 粤嵌.温工
 * @date 2026-02-24
 * @version 1.0.0
 * 
 * @section 使用说明
 * 
 * 1. 初始化DHT11传感器：
 *    - 在使用DHT11传感器之前，必须先调用Initialize()方法进行初始化
 *    - 初始化过程会生成初始虚拟温湿度数据
 *    - 初始化成功返回true，失败返回false
 * 
 * 2. 获取温湿度数据：
 *    - 使用GetTemperature()方法获取当前温度值
 *    - 使用GetHumidity()方法获取当前湿度值
 *    - 使用GetTemperatureAndHumidity()方法同时获取温湿度值
 * 
 * 3. 设备唤醒时调整温度：
 *    - 使用OnWakeUp()方法在设备唤醒时调整温度值
 *    - 每次设备被唤醒时，温度值额外增加0.5℃
 *    - 温度值上限为40.0℃，超过后重置为20.0℃
 * 
 * 4. 获取字符串格式的温湿度数据：
 *    - 使用GetTemperatureString()方法获取温度值的字符串表示
 *    - 使用GetHumidityString()方法获取湿度值的字符串表示
 *    - 使用GetTemperatureAndHumidityString()方法获取温湿度值的字符串表示
 * 
 * 5. MCP协议集成：
 *    - 该模块已集成MCP协议，支持通过MCP工具调用
 *    - 可用的MCP工具：
 *      - self.dht11.get_temperature：获取温度值
 *      - self.dht11.get_humidity：获取湿度值
 *      - self.dht11.get_temperature_and_humidity：获取温湿度值
 * 
 * 6. 示例代码：
 *    @code
 *    #include "dht11_sensor.h"
 *    
 *    // 创建DHT11传感器实例
 *    Dht11Sensor dht11_sensor;
 *    
 *    // 初始化DHT11传感器
 *    if (!dht11_sensor.Initialize()) {
 *        ESP_LOGE(TAG, "DHT11传感器初始化失败");
 *        return;
 *    }
 *    
 *    // 获取温度值
 *    float temperature = dht11_sensor.GetTemperature();
 *    ESP_LOGI(TAG, "当前温度: %.1f℃", temperature);
 *    
 *    // 获取湿度值
 *    float humidity = dht11_sensor.GetHumidity();
 *    ESP_LOGI(TAG, "当前湿度: %.1f%%", humidity);
 *    
 *    // 获取温湿度值
 *    float temp, hum;
 *    if (dht11_sensor.GetTemperatureAndHumidity(temp, hum)) {
 *        ESP_LOGI(TAG, "温度: %.1f℃, 湿度: %.1f%%", temp, hum);
 *    }
 *    
 *    // 设备唤醒时调整温度
 *    dht11_sensor.OnWakeUp();
 *    
 *    // 获取字符串格式的温湿度数据
 *    std::string temp_str = dht11_sensor.GetTemperatureString();
 *    std::string hum_str = dht11_sensor.GetHumidityString();
 *    std::string temp_hum_str = dht11_sensor.GetTemperatureAndHumidityString();
 *    ESP_LOGI(TAG, "温度: %s", temp_str.c_str());
 *    ESP_LOGI(TAG, "湿度: %s", hum_str.c_str());
 *    ESP_LOGI(TAG, "%s", temp_hum_str.c_str());
 *    @endcode
 * 
 * 7. 虚拟数据说明：
 *    - 温度值范围：20.0℃ - 30.0℃（初始生成）
 *    - 湿度值范围：40.0% - 80.0%（初始生成）
 *    - 设备唤醒时温度值增加0.5℃
 *    - 温度值上限为40.0℃，超过后重置为20.0℃
 *    - 湿度值在设备唤醒时重新生成
 * 
 * 8. 性能说明：
 *    - 数据生成：使用随机数生成，模拟真实传感器数据
 *    - 数据访问：内存中存储，访问速度快
 *    - 响应时间：微秒级
 *    - 内存占用：约8字节（两个float成员变量）
 * 
 * 9. 兼容性：
 *    - 支持ESP32全系列芯片
 *    - 兼容ESP-IDF v4.4及以上版本
 *    - 可与其他模块同时使用
 *    - 支持跨平台（Windows、Linux）
 * 
 * 10. 注意事项：
 *     - 该类提供虚拟数据，不连接实际硬件
 *     - 温度值在设备唤醒时会动态调整
 *     - 湿度值在设备唤醒时会重新生成
 *     - 温度值上限为40.0℃，超过后重置为20.0℃
 *     - 虚拟数据用于测试和演示，实际应用需要连接真实传感器
 */

#ifndef DHT11_SENSOR_H
#define DHT11_SENSOR_H

#include <string>

/**
 * @brief DHT11温湿度传感器类
 * 
 * 该类提供虚拟温湿度数据生成功能，用于模拟DHT11传感器的数据读取
 * 支持通过语音指令获取温度、湿度及温湿度值
 * 
 * @note 该类提供虚拟数据，不连接实际硬件
 * @note 温度值在设备唤醒时会动态调整
 * @note 湿度值在设备唤醒时会重新生成
 * @note 该类是单例模式，建议在Application类中作为成员变量使用
 * 
 * @section 虚拟数据生成
 * 
 * 1. 初始数据生成：
 *    - 温度值：20.0℃ - 30.0℃之间的随机值
 *    - 湿度值：40.0% - 80.0%之间的随机值
 * 
 * 2. 设备唤醒时数据调整：
 *    - 温度值：在当前值基础上增加0.5℃
 *    - 温度值上限：40.0℃，超过后重置为20.0℃
 *    - 湿度值：重新生成40.0% - 80.0%之间的随机值
 * 
 * @section MCP工具
 * 
 * 可用的MCP工具：
 * - self.dht11.get_temperature：获取温度值
 * - self.dht11.get_humidity：获取湿度值
 * - self.dht11.get_temperature_and_humidity：获取温湿度值
 */
class Dht11Sensor {
private:
    float temperature_;  // 当前温度值（摄氏度）
    float humidity_;    // 当前湿度值（百分比）
    bool initialized_;  // 初始化状态标志，true表示已初始化

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
    float GenerateVirtualTemperature();

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
    float GenerateVirtualHumidity();

public:
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
    Dht11Sensor();

    /**
     * @brief 析构函数
     * 
     * 清理DHT11传感器资源
     * 
     * @note 析构函数是线程安全的
     */
    ~Dht11Sensor();

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
    bool Initialize();

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
    float GetTemperature();

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
    float GetHumidity();

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
    bool GetTemperatureAndHumidity(float& temperature, float& humidity);

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
    void OnWakeUp();

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
    std::string GetTemperatureString();

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
    std::string GetHumidityString();

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
    std::string GetTemperatureAndHumidityString();
};

#endif // DHT11_SENSOR_H
