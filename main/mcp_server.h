/**
 * @file mcp_server.h
 * @brief MCP (Model Context Protocol) 服务器头文件
 * 
 * @details
 * 本文件定义了MCP服务器的核心接口和数据结构，用于实现与AI模型的工具调用协议。
 * MCP协议允许AI模型通过标准化的接口调用设备上的各种功能（工具）。
 * 
 * 主要功能：
 * - 工具定义和管理（McpTool类）
 * - 属性定义和验证（Property类）
 * - JSON序列化和反序列化
 * - 工具调用执行和结果返回
 * 
 * 使用示例：
 * @code
 * // 1. 获取MCP服务器单例实例
 * auto& mcp_server = McpServer::GetInstance();
 * 
 * // 2. 添加常用工具
 * mcp_server.AddCommonTools();
 * 
 * // 3. 定义自定义工具
 * mcp_server.AddTool("custom.tool",
 *     "Description of the tool",
 *     PropertyList({
 *         Property("param1", kPropertyTypeString),
 *         Property("param2", kPropertyTypeInteger, 0, 100)
 *     }),
 *     [](const PropertyList& props) -> ReturnValue {
 *         auto value = props["param1"].value<std::string>();
 *         return "Result: " + value;
 *     });
 * 
 * // 4. 解析并执行工具调用
 * mcp_server.ParseMessage(json_message);
 * @endcode
 * 
 * @note 基于Model Context Protocol规范实现
 * @see https://modelcontextprotocol.io/specification/2024-11-05
 */

#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <variant>
#include <optional>
#include <stdexcept>
#include <thread>

#include <cJSON.h>

/**
 * @brief 工具返回值类型
 * @details 定义工具回调函数可以返回的值的类型，支持布尔值、整数和字符串
 */
using ReturnValue = std::variant<bool, int, std::string>;

/**
 * @brief 属性类型枚举
 * @details 定义Property支持的数据类型
 */
enum PropertyType {
    kPropertyTypeBoolean,   ///< 布尔类型
    kPropertyTypeInteger,   ///< 整数类型
    kPropertyTypeString     ///< 字符串类型
};

/**
 * @brief 工具属性类
 * @details 定义工具参数的属性，包括名称、类型、默认值和范围限制
 * 
 * 使用场景：
 * - 定义工具所需的输入参数
 * - 设置参数的默认值
 * - 对整数参数设置范围限制（最小值/最大值）
 * 
 * 示例：
 * @code
 * // 必需参数（无默认值）
 * Property name_param("name", kPropertyTypeString);
 * 
 * // 可选参数（有默认值）
 * Property volume_param("volume", kPropertyTypeInteger, 50);
 * 
 * // 带范围限制的整数参数
 * Property brightness_param("brightness", kPropertyTypeInteger, 0, 100);
 * 
 * // 带默认值和范围限制的参数
 * Property timeout_param("timeout", kPropertyTypeInteger, 30, 1, 300);
 * @endcode
 */
class Property {
private:
    std::string name_;                                          ///< 属性名称
    PropertyType type_;                                         ///< 属性类型
    std::variant<bool, int, std::string> value_;               ///< 属性值存储
    bool has_default_value_;                                    ///< 是否有默认值
    std::optional<int> min_value_;                             ///< 整数最小值（可选）
    std::optional<int> max_value_;                             ///< 整数最大值（可选）

public:
    /**
     * @brief 必需字段构造函数
     * @param name 属性名称
     * @param type 属性类型
     * @details 创建一个没有默认值的必需参数
     */
    Property(const std::string& name, PropertyType type)
        : name_(name), type_(type), has_default_value_(false) {}

    /**
     * @brief 可选字段构造函数（带默认值）
     * @param name 属性名称
     * @param type 属性类型
     * @param default_value 默认值
     * @details 创建一个有默认值的可选参数
     */
    template<typename T>
    Property(const std::string& name, PropertyType type, const T& default_value)
        : name_(name), type_(type), has_default_value_(true) {
        value_ = default_value;
    }

    /**
     * @brief 带范围限制的整数参数构造函数
     * @param name 属性名称
     * @param type 属性类型（必须是kPropertyTypeInteger）
     * @param min_value 最小值
     * @param max_value 最大值
     * @throws std::invalid_argument 如果类型不是整数类型
     */
    Property(const std::string& name, PropertyType type, int min_value, int max_value)
        : name_(name), type_(type), has_default_value_(false), min_value_(min_value), max_value_(max_value) {
        if (type != kPropertyTypeInteger) {
            throw std::invalid_argument("Range limits only apply to integer properties");
        }
    }

    /**
     * @brief 带默认值和范围限制的整数参数构造函数
     * @param name 属性名称
     * @param type 属性类型（必须是kPropertyTypeInteger）
     * @param default_value 默认值
     * @param min_value 最小值
     * @param max_value 最大值
     * @throws std::invalid_argument 如果类型不是整数类型或默认值超出范围
     */
    Property(const std::string& name, PropertyType type, int default_value, int min_value, int max_value)
        : name_(name), type_(type), has_default_value_(true), min_value_(min_value), max_value_(max_value) {
        if (type != kPropertyTypeInteger) {
            throw std::invalid_argument("Range limits only apply to integer properties");
        }
        if (default_value < min_value || default_value > max_value) {
            throw std::invalid_argument("Default value must be within the specified range");
        }
        value_ = default_value;
    }

    /**
     * @brief 获取属性名称
     * @return 属性名称的常量引用
     */
    inline const std::string& name() const { return name_; }

    /**
     * @brief 获取属性类型
     * @return 属性类型枚举值
     */
    inline PropertyType type() const { return type_; }

    /**
     * @brief 检查是否有默认值
     * @return true表示有默认值，false表示没有
     */
    inline bool has_default_value() const { return has_default_value_; }

    /**
     * @brief 检查是否有范围限制
     * @return true表示有最小值和最大值限制
     */
    inline bool has_range() const { return min_value_.has_value() && max_value_.has_value(); }

    /**
     * @brief 获取最小值
     * @return 最小值，如果没有设置则返回0
     */
    inline int min_value() const { return min_value_.value_or(0); }

    /**
     * @brief 获取最大值
     * @return 最大值，如果没有设置则返回0
     */
    inline int max_value() const { return max_value_.value_or(0); }

    /**
     * @brief 获取属性值
     * @tparam T 要获取的值类型（bool, int, 或 std::string）
     * @return 属性值
     * @throws std::bad_variant_access 如果存储的类型与请求的类型不匹配
     */
    template<typename T>
    inline T value() const {
        return std::get<T>(value_);
    }

    /**
     * @brief 设置属性值
     * @tparam T 值类型
     * @param value 要设置的值
     * @throws std::invalid_argument 如果整数值超出范围限制
     * @details 对于整数类型，会自动进行范围验证
     */
    template<typename T>
    inline void set_value(const T& value) {
        // 对整数类型进行范围检查
        if constexpr (std::is_same_v<T, int>) {
            if (min_value_.has_value() && value < min_value_.value()) {
                throw std::invalid_argument("Value is below minimum allowed: " + std::to_string(min_value_.value()));
            }
            if (max_value_.has_value() && value > max_value_.value()) {
                throw std::invalid_argument("Value exceeds maximum allowed: " + std::to_string(max_value_.value()));
            }
        }
        value_ = value;
    }

    /**
     * @brief 将属性转换为JSON格式
     * @return JSON字符串表示
     * @details 生成的JSON包含类型、默认值、最小值/最大值等信息
     */
    std::string to_json() const {
        cJSON *json = cJSON_CreateObject();
        
        if (type_ == kPropertyTypeBoolean) {
            cJSON_AddStringToObject(json, "type", "boolean");
            if (has_default_value_) {
                cJSON_AddBoolToObject(json, "default", value<bool>());
            }
        } else if (type_ == kPropertyTypeInteger) {
            cJSON_AddStringToObject(json, "type", "integer");
            if (has_default_value_) {
                cJSON_AddNumberToObject(json, "default", value<int>());
            }
            if (min_value_.has_value()) {
                cJSON_AddNumberToObject(json, "minimum", min_value_.value());
            }
            if (max_value_.has_value()) {
                cJSON_AddNumberToObject(json, "maximum", max_value_.value());
            }
        } else if (type_ == kPropertyTypeString) {
            cJSON_AddStringToObject(json, "type", "string");
            if (has_default_value_) {
                cJSON_AddStringToObject(json, "default", value<std::string>().c_str());
            }
        }
        
        char *json_str = cJSON_PrintUnformatted(json);
        std::string result(json_str);
        cJSON_free(json_str);
        cJSON_Delete(json);
        
        return result;
    }
};

/**
 * @brief 属性列表类
 * @details 管理工具的所有属性，提供属性集合操作和JSON序列化
 * 
 * 使用场景：
 * - 定义工具的参数列表
 * - 批量管理多个Property对象
 * - 生成工具的JSON Schema
 * 
 * 示例：
 * @code
 * PropertyList params({
 *     Property("name", kPropertyTypeString),
 *     Property("age", kPropertyTypeInteger, 0, 150),
 *     Property("active", kPropertyTypeBoolean, true)
 * });
 * 
 * // 访问属性
 * auto name = params["name"].value<std::string>();
 * 
 * // 获取必需参数列表
 * auto required = params.GetRequired();
 * @endcode
 */
class PropertyList {
private:
    std::vector<Property> properties_;  ///< 属性存储容器

public:
    /**
     * @brief 默认构造函数
     * @details 创建一个空的属性列表
     */
    PropertyList() = default;

    /**
     * @brief 带初始值的构造函数
     * @param properties 初始属性向量
     */
    PropertyList(const std::vector<Property>& properties) : properties_(properties) {}

    /**
     * @brief 添加属性到列表
     * @param property 要添加的属性
     */
    void AddProperty(const Property& property) {
        properties_.push_back(property);
    }

    /**
     * @brief 通过名称访问属性
     * @param name 属性名称
     * @return 属性的常量引用
     * @throws std::runtime_error 如果属性不存在
     */
    const Property& operator[](const std::string& name) const {
        for (const auto& property : properties_) {
            if (property.name() == name) {
                return property;
            }
        }
        throw std::runtime_error("Property not found: " + name);
    }

    /**
     * @brief 获取属性列表的起始迭代器
     * @return 起始迭代器
     */
    auto begin() { return properties_.begin(); }

    /**
     * @brief 获取属性列表的结束迭代器
     * @return 结束迭代器
     */
    auto end() { return properties_.end(); }

    /**
     * @brief 获取所有必需参数（无默认值的参数）的名称列表
     * @return 必需参数名称的向量
     * @details 用于生成JSON Schema的required字段
     */
    std::vector<std::string> GetRequired() const {
        std::vector<std::string> required;
        for (auto& property : properties_) {
            if (!property.has_default_value()) {
                required.push_back(property.name());
            }
        }
        return required;
    }

    /**
     * @brief 将属性列表转换为JSON格式
     * @return JSON字符串表示
     * @details 生成符合JSON Schema格式的属性定义
     */
    std::string to_json() const {
        cJSON *json = cJSON_CreateObject();
        
        for (const auto& property : properties_) {
            cJSON *prop_json = cJSON_Parse(property.to_json().c_str());
            cJSON_AddItemToObject(json, property.name().c_str(), prop_json);
        }
        
        char *json_str = cJSON_PrintUnformatted(json);
        std::string result(json_str);
        cJSON_free(json_str);
        cJSON_Delete(json);
        
        return result;
    }
};

/**
 * @brief MCP工具类
 * @details 表示一个可被AI模型调用的工具，包含工具元数据和执行回调
 * 
 * 工具定义包括：
 * - 名称：唯一标识符
 * - 描述：AI模型理解工具用途的说明
 * - 参数：工具所需的输入参数定义
 * - 回调：工具被调用时执行的函数
 * 
 * 示例：
 * @code
 * McpTool volume_tool(
 *     "audio.set_volume",
 *     "Set the audio volume level",
 *     PropertyList({Property("level", kPropertyTypeInteger, 0, 100)}),
 *     [](const PropertyList& props) -> ReturnValue {
 *         int level = props["level"].value<int>();
 *         // 设置音量...
 *         return true;
 *     }
 * );
 * @endcode
 */
class McpTool {
private:
    std::string name_;                                          ///< 工具名称
    std::string description_;                                   ///< 工具描述
    PropertyList properties_;                                   ///< 工具参数列表
    std::function<ReturnValue(const PropertyList&)> callback_; ///< 工具执行回调
    bool user_only_ = false;                                    ///< 是否仅用户可见（对AI隐藏）

public:
    /**
     * @brief 构造函数
     * @param name 工具名称
     * @param description 工具描述
     * @param properties 工具参数列表
     * @param callback 工具执行回调函数
     */
    McpTool(const std::string& name, 
            const std::string& description, 
            const PropertyList& properties, 
            std::function<ReturnValue(const PropertyList&)> callback)
        : name_(name), 
        description_(description), 
        properties_(properties), 
        callback_(callback) {}

    /**
     * @brief 设置工具的用户可见性
     * @param user_only true表示仅用户可见，false表示AI和用户都可见
     * @details user_only=true的工具不会出现在AI的工具列表中
     */
    void set_user_only(bool user_only) { user_only_ = user_only; }

    /**
     * @brief 获取工具名称
     * @return 工具名称的常量引用
     */
    inline const std::string& name() const { return name_; }

    /**
     * @brief 获取工具描述
     * @return 工具描述的常量引用
     */
    inline const std::string& description() const { return description_; }

    /**
     * @brief 获取工具参数列表
     * @return 参数列表的常量引用
     */
    inline const PropertyList& properties() const { return properties_; }

    /**
     * @brief 检查工具是否仅用户可见
     * @return true表示仅用户可见
     */
    inline bool user_only() const { return user_only_; }

    /**
     * @brief 将工具定义转换为JSON格式
     * @return 符合MCP协议的工具定义JSON字符串
     * @details 生成的JSON包含名称、描述、输入参数schema和受众注解
     */
    std::string to_json() const {
        std::vector<std::string> required = properties_.GetRequired();
        
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "name", name_.c_str());
        cJSON_AddStringToObject(json, "description", description_.c_str());
        
        cJSON *input_schema = cJSON_CreateObject();
        cJSON_AddStringToObject(input_schema, "type", "object");
        
        cJSON *properties = cJSON_Parse(properties_.to_json().c_str());
        cJSON_AddItemToObject(input_schema, "properties", properties);
        
        if (!required.empty()) {
            cJSON *required_array = cJSON_CreateArray();
            for (const auto& property : required) {
                cJSON_AddItemToArray(required_array, cJSON_CreateString(property.c_str()));
            }
            cJSON_AddItemToObject(input_schema, "required", required_array);
        }
        
        cJSON_AddItemToObject(json, "inputSchema", input_schema);

        // Add audience annotation if the tool is user only (invisible to AI)
        if (user_only_) {
            cJSON *annotations = cJSON_CreateObject();
            cJSON *audience = cJSON_CreateArray();
            cJSON_AddItemToArray(audience, cJSON_CreateString("user"));
            cJSON_AddItemToObject(annotations, "audience", audience);
            cJSON_AddItemToObject(json, "annotations", annotations);
        }
        
        char *json_str = cJSON_PrintUnformatted(json);
        std::string result(json_str);
        cJSON_free(json_str);
        cJSON_Delete(json);
        
        return result;
    }

    /**
     * @brief 执行工具调用
     * @param properties 工具调用时传入的参数值
     * @return 工具执行结果的JSON字符串
     * @details 调用回调函数执行工具逻辑，并将返回值格式化为MCP协议要求的JSON格式
     * 
     * 返回格式：
     * @code
     * {
     *     "content": [{"type": "text", "text": "result"}],
     *     "isError": false
     * }
     * @endcode
     */
    std::string Call(const PropertyList& properties) {
        ReturnValue return_value = callback_(properties);
        // 创建结果JSON对象
        cJSON* result = cJSON_CreateObject();
        cJSON* content = cJSON_CreateArray();
        cJSON* text = cJSON_CreateObject();
        cJSON_AddStringToObject(text, "type", "text");
        // 根据返回值类型序列化为字符串
        if (std::holds_alternative<std::string>(return_value)) {
            cJSON_AddStringToObject(text, "text", std::get<std::string>(return_value).c_str());
        } else if (std::holds_alternative<bool>(return_value)) {
            cJSON_AddStringToObject(text, "text", std::get<bool>(return_value) ? "true" : "false");
        } else if (std::holds_alternative<int>(return_value)) {
            cJSON_AddStringToObject(text, "text", std::to_string(std::get<int>(return_value)).c_str());
        }
        cJSON_AddItemToArray(content, text);
        cJSON_AddItemToObject(result, "content", content);
        cJSON_AddBoolToObject(result, "isError", false);

        auto json_str = cJSON_PrintUnformatted(result);
        std::string result_str(json_str);
        cJSON_free(json_str);
        cJSON_Delete(result);
        return result_str;
    }
};

/**
 * @brief MCP服务器类
 * @details 单例模式实现的MCP协议服务器，管理所有工具并提供协议处理功能
 * 
 * 核心功能：
 * - 工具注册和管理
 * - MCP协议消息解析和处理
 * - 工具列表查询
 * - 工具调用执行
 * 
 * 使用示例：
 * @code
 * // 获取服务器实例
 * auto& server = McpServer::GetInstance();
 * 
 * // 添加常用工具
 * server.AddCommonTools();
 * 
 * // 添加自定义工具
 * server.AddTool("custom.action", "Description",
 *     PropertyList({Property("param", kPropertyTypeString)}),
 *     [](const PropertyList& props) -> ReturnValue {
 *         return "Done";
 *     });
 * 
 * // 解析MCP消息
 * server.ParseMessage(json_string);
 * @endcode
 * 
 * @note 使用单例模式，全局只有一个实例
 */
class McpServer {
public:
    /**
     * @brief 获取McpServer单例实例
     * @return McpServer实例的引用
     * @details 线程安全的单例实现（C++11及以上）
     */
    static McpServer& GetInstance() {
        static McpServer instance;
        return instance;
    }

    /**
     * @brief 添加常用工具
     * @details 注册一组预定义的设备控制工具，包括：
     * - 设备状态查询
     * - 音量控制
     * - 屏幕亮度控制
     * - 主题设置
     * - 摄像头拍照
     * - LED控制
     * - DHT11传感器读取
     * - MyInfo信息播放
     */
    void AddCommonTools();

    /**
     * @brief 添加工具（通过指针）
     * @param tool 工具对象指针，由McpServer管理生命周期
     * @details 将工具添加到工具列表，如果同名工具已存在则忽略
     */
    void AddTool(McpTool* tool);

    /**
     * @brief 添加工具（通过参数）
     * @param name 工具名称
     * @param description 工具描述
     * @param properties 工具参数列表
     * @param callback 工具执行回调函数
     * @details 便捷方法，自动创建McpTool对象
     */
    void AddTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback);

    /**
     * @brief 添加仅用户可见的工具
     * @param name 工具名称
     * @param description 工具描述
     * @param properties 工具参数列表
     * @param callback 工具执行回调函数
     * @details 创建的工具对AI不可见，仅用于用户直接调用
     */
    void AddUserOnlyTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback);

    /**
     * @brief 解析MCP协议消息（cJSON格式）
     * @param json 解析后的cJSON对象
     * @details 处理MCP协议的各种请求（工具列表、工具调用等）
     */
    void ParseMessage(const cJSON* json);

    /**
     * @brief 解析MCP协议消息（字符串格式）
     * @param message JSON格式的消息字符串
     * @details 自动解析JSON字符串并调用ParseMessage(cJSON*)
     */
    void ParseMessage(const std::string& message);

private:
    /**
     * @brief 私有构造函数
     * @details 单例模式，禁止外部实例化
     */
    McpServer();

    /**
     * @brief 析构函数
     * @details 释放所有工具对象
     */
    ~McpServer();

    /**
     * @brief 解析客户端能力
     * @param capabilities cJSON格式的能力描述对象
     */
    void ParseCapabilities(const cJSON* capabilities);

    /**
     * @brief 发送成功响应
     * @param id 请求ID
     * @param result 结果数据JSON字符串
     */
    void ReplyResult(int id, const std::string& result);

    /**
     * @brief 发送错误响应
     * @param id 请求ID
     * @param message 错误消息
     */
    void ReplyError(int id, const std::string& message);

    /**
     * @brief 处理工具列表请求
     * @param id 请求ID
     * @param cursor 分页游标（用于大量工具的分页）
     */
    void GetToolsList(int id, const std::string& cursor);

    /**
     * @brief 执行工具调用
     * @param id 请求ID
     * @param tool_name 要调用的工具名称
     * @param tool_arguments 工具参数（cJSON对象）
     */
    void DoToolCall(int id, const std::string& tool_name, const cJSON* tool_arguments);

    std::vector<McpTool*> tools_;  ///< 工具列表
};

#endif // MCP_SERVER_H
