/**
 * @file myinfo.h
 * @brief MyInfo模块头文件，提供信息存储和语音播报功能
 * 
 * 本文件定义了MyInfo类及相关结构体和枚举，用于存储信息并通过语音命令触发播报。
 * 该模块支持MCP协议，可以通过语音指令播报指定信息或所有信息。
 * 
 * @author 粤嵌.温工
 * @date 2026-02-24
 * @version 1.0.0
 * 
 * @section 使用说明
 * 
 * 1. 初始化MyInfo模块：
 *    - 在使用MyInfo模块之前，必须先调用Initialize()方法进行初始化
 *    - 初始化过程会加载三条预定义信息到内存中
 *    - 初始化成功返回MyInfoError::SUCCESS，失败返回相应的错误码
 * 
 * 2. 设置语音播放回调函数：
 *    - 使用SetPlayVoiceCallback()方法设置语音播放回调函数
 *    - 回调函数负责将文本转换为语音并播放
 *    - 回调函数类型为std::function<void(const std::string&)>
 * 
 * 3. 处理语音命令：
 *    - 使用ProcessVoiceCommand()方法处理语音命令
 *    - 支持的语音命令：
 *      - "介绍温工"、"温工是谁"、"info1"：播报信息1
 *      - "介绍粤嵌公司"、"粤嵌公司"、"info2"：播报信息2
 *      - "介绍林院"、"林院是谁"、"info3"：播报信息3
 *      - "介绍嵌入式与AI结合的发展前景"、"嵌入式与AI结合的发展前景"、"边缘智能"、"info4"：播报信息4
 *      - "所有信息"、"播报所有信息"、"allinfo"：播报所有信息
 * 
 * 4. 直接控制信息播报：
 *    - 使用PlayInfo()方法播报指定信息（索引1、2、3、4）
 *    - 使用PlayAllInfo()方法播报所有信息
 *    - 使用TextToSpeech()方法将任意文本转换为语音
 * 
 * 5. 查询信息内容：
 *    - 使用GetInfo()方法根据索引获取指定信息
 *    - 使用GetInfoById()方法根据ID获取指定信息
 *    - 使用GetInfoCount()方法获取所有信息数量
 * 
 * 6. MCP协议集成：
 *    - 该模块已集成MCP协议，支持通过MCP工具调用
 *    - 可用的MCP工具：
 *      - self.myinfo.play_info：播报指定信息
 *      - self.myinfo.play_all_info：播报所有信息
 *      - self.myinfo.get_info：获取指定信息
 *      - self.myinfo.get_info_count：获取信息总数
 * 
 * 7. 示例代码：
 *    @code
 *    #include "myinfo.h"
 *    
 *    // 创建MyInfo实例
 *    MyInfo myinfo;
 *    
 *    // 初始化MyInfo模块
 *    if (myinfo.Initialize() != MyInfoError::SUCCESS) {
 *        ESP_LOGE(TAG, "MyInfo模块初始化失败");
 *        return;
 *    }
 *    
 *    // 设置语音播放回调函数
 *    myinfo.SetPlayVoiceCallback([](const std::string& text) {
 *        ESP_LOGI(TAG, "播放语音: %s", text.c_str());
 *        // 调用语音播放模块播放文本
 *    });
 *    
 *    // 处理语音命令
 *    MyInfoError result = myinfo.ProcessVoiceCommand("介绍温工");
 *    if (result != MyInfoError::SUCCESS) {
 *        ESP_LOGE(TAG, "处理语音命令失败: %s", myinfo.GetErrorMessage(result).c_str());
 *    }
 *    
 *    // 直接播报信息
 *    result = myinfo.PlayInfo(1);
 *    if (result != MyInfoError::SUCCESS) {
 *        ESP_LOGE(TAG, "播报信息失败: %s", myinfo.GetErrorMessage(result).c_str());
 *    }
 *    
 *    // 获取信息内容
 *    InfoEntry info;
 *    result = myinfo.GetInfo(1, info);
 *    if (result == MyInfoError::SUCCESS) {
 *        ESP_LOGI(TAG, "信息标题: %s", info.title.c_str());
 *        ESP_LOGI(TAG, "信息内容: %s", info.content.c_str());
 *    }
 *    @endcode
 * 
 * 8. 错误处理：
 *    - MyInfoError::SUCCESS：操作成功
 *    - MyInfoError::INFO_NOT_FOUND：信息未找到
 *    - MyInfoError::INVALID_INDEX：无效的信息索引
 *    - MyInfoError::TTS_FAILED：语音合成失败
 *    - MyInfoError::INIT_FAILED：初始化失败
 *    - MyInfoError::INVALID_COMMAND：无效的命令
 * 
 * 9. 性能说明：
 *    - 信息存储：内存中存储，访问速度快
 *    - 语音命令解析：字符串匹配，响应时间毫秒级
 *    - 语音播放：取决于语音引擎性能
 *    - 内存占用：约1.5KB（四条信息）
 * 
 * 10. 兼容性：
 *     - 支持ESP32全系列芯片
 *     - 兼容ESP-IDF v4.4及以上版本
 *     - 可与其他模块同时使用
 *     - 支持跨平台（Windows、Linux）
 * 
 * 11. 注意事项：
 *     - 必须先初始化模块，再进行其他操作
 *     - 必须设置语音播放回调函数，才能播放语音
 *     - 语音命令解析不区分大小写
 *     - 语音命令解析会自动去除空格
 *     - 信息索引从1开始，不是0
 */

#ifndef MYINFO_H
#define MYINFO_H

#include <string>
#include <vector>
#include <functional>

/**
 * @brief 信息条目结构体
 * 
 * 用于存储单条信息的内容和标识符
 * 每条信息包含ID、标题、内容和索引
 * 
 * @note ID是信息的唯一标识符，用于通过ID查找信息
 * @note 标题是信息的简短描述，便于用户识别
 * @note 内容是信息的详细文本，用于语音播报
 * @note 索引是信息的序号，从1开始递增
 * 
 * @see MyInfo::GetInfo()
 * @see MyInfo::GetInfoById()
 */
struct InfoEntry {
    std::string id;        // 信息标识符（如"info1"、"info2"、"info3"）
    std::string title;      // 信息标题
    std::string content;    // 信息内容
    int index;             // 信息索引（1、2、3、4）
};

/**
 * @brief 语音播报命令类型枚举
 * 
 * 定义了所有支持的语音播报命令类型
 * 用于语音命令解析和执行
 * 
 * @note PLAY_INFO_1、PLAY_INFO_2、PLAY_INFO_3、PLAY_INFO_4：播报指定信息
 * @note PLAY_ALL_INFO：播报所有信息
 * @note UNKNOWN_COMMAND：未知命令
 * 
 * @see MyInfo::ParseVoiceCommand()
 * @see MyInfo::ProcessVoiceCommand()
 */
enum class VoiceCommand {
    PLAY_INFO_1,          // 播报信息1
    PLAY_INFO_2,          // 播报信息2
    PLAY_INFO_3,          // 播报信息3
    PLAY_INFO_4,          // 播报信息4
    PLAY_ALL_INFO,         // 播报所有信息
    UNKNOWN_COMMAND         // 未知命令
};

/**
 * @brief 错误代码枚举
 * 
 * 定义了MyInfo模块的所有错误代码
 * 用于错误处理和错误信息返回
 * 
 * @note SUCCESS：操作成功
 * @note INFO_NOT_FOUND：信息未找到
 * @note INVALID_INDEX：无效的信息索引
 * @note TTS_FAILED：语音合成失败
 * @note INIT_FAILED：初始化失败
 * @note INVALID_COMMAND：无效的命令
 * 
 * @see MyInfo::GetErrorMessage()
 */
enum class MyInfoError {
    SUCCESS = 0,           // 成功
    INFO_NOT_FOUND,         // 信息未找到
    INVALID_INDEX,          // 无效索引
    TTS_FAILED,            // 语音合成失败
    INIT_FAILED,           // 初始化失败
    INVALID_COMMAND          // 无效命令
};

/**
 * @brief MyInfo类
 * 
 * 提供信息存储和语音播报功能，支持通过语音命令触发信息播报
 * 该类支持MCP协议，可以通过MCP工具调用
 * 
 * @note 该类是单例模式，建议在Application类中作为成员变量使用
 * @note 必须先初始化模块，再进行其他操作
 * @note 必须设置语音播放回调函数，才能播放语音
 * 
 * @section 预定义信息
 * 
 * 1. 信息1（info1）：
 *    - 标题：介绍温工
 *    - 内容：温工程师 - 粤嵌科技技术专家，10年+嵌入式开发经验，ESP32物联网专家，专长：嵌入式系统、AI语音、工业物联网，代表项目：智能语音助手、工业物联网网关
 * 
 * 2. 信息2（info2）：
 *    - 标题：介绍粤嵌公司
 *    - 内容：广州粤嵌通信科技股份有限公司，国家级高新技术企业，成立于2005年，核心业务：嵌入式培训、物联网研发、AI解决方案，官网：www.yueqian.com.cn
 * 
 * 3. 信息3（info3）：
 *    - 标题：介绍林院
 *    - 内容：林院，特级讲师，视觉设计类教学专家，有逾十年的设计教学经验，在视觉设计闭环式教学上有自己独到的一套方法论与教育哲学。对商业目标和用户行为具有前瞻性思考，能够在复杂业务场景中洞察本质，持续稳定输出优秀设计方案，对设计结果呈现能精准预判。
 * 
 * 4. 信息4（info4）：
 *    - 标题：介绍嵌入式与AI结合的发展前景
 *    - 内容：嵌入式加AI（边缘智能）前景广阔。行业需求旺盛，智能硬件市场庞大，人才缺口显著。掌握AI部署、模型优化、RISC-V及异构计算技能的复合型工程师将处于高薪、无35岁危机的黄金赛道。建议从基础扎实，快速切入边缘AI项目实践。
 * 
 * @section 语音命令
 * 
 * 支持的语音命令（不区分大小写，自动去除空格）：
 * - "介绍温工"、"温工是谁"、"info1"：播报信息1
 * - "介绍粤嵌公司"、"粤嵌公司"、"info2"：播报信息2
 * - "介绍林院"、"林院是谁"、"info3"：播报信息3
 * - "介绍嵌入式与AI结合的发展前景"、"嵌入式与AI结合的发展前景"、"边缘智能"、"info4"：播报信息4
 * - "所有信息"、"播报所有信息"、"allinfo"：播报所有信息
 * 
 * @section MCP工具
 * 
 * 可用的MCP工具：
 * - self.myinfo.play_info：播报指定信息（需要index参数）
 * - self.myinfo.play_all_info：播报所有信息
 * - self.myinfo.get_info：获取指定信息（需要index参数）
 * - self.myinfo.get_info_count：获取信息总数
 */
class MyInfo {
private:
    std::vector<InfoEntry> info_entries_;  // 信息条目列表，存储所有预定义信息
    bool initialized_;                      // 初始化状态标志，true表示已初始化
    
    // 语音播放回调函数类型定义
    // 回调函数接收一个字符串参数，表示要播放的文本
    using PlayVoiceCallback = std::function<void(const std::string&)>;
    PlayVoiceCallback play_voice_callback_;     // 语音播放回调函数，用于将文本转换为语音并播放
    
    /**
     * @brief 解析语音命令
     * 
     * 将语音命令字符串解析为VoiceCommand枚举类型
     * 解析过程不区分大小写，并自动去除空格
     * 
     * @param command 语音命令字符串
     * @return VoiceCommand 解析后的命令类型
     * 
     * @note 如果命令无法识别，返回VoiceCommand::UNKNOWN_COMMAND
     * @note 支持多种命令格式，提高识别准确性
     * 
     * @see VoiceCommand
     * @see ProcessVoiceCommand()
     */
    VoiceCommand ParseVoiceCommand(const std::string& command);
    
    /**
     * @brief 获取错误信息字符串
     * 
     * 将错误代码转换为可读的错误信息字符串
     * 用于错误日志输出和用户提示
     * 
     * @param error 错误代码
     * @return std::string 错误信息字符串
     * 
     * @note 如果错误代码未知，返回"未知错误"
     * 
     * @see MyInfoError
     */
    std::string GetErrorMessage(MyInfoError error);

public:
    /**
     * @brief 构造函数
     * 
     * 初始化MyInfo类的成员变量
     * 将初始化状态设置为false
     * 
     * @note 构造函数不会自动初始化模块，需要显式调用Initialize()方法
     * @note 构造函数是线程安全的
     * 
     * @see Initialize()
     */
    MyInfo();
    
    /**
     * @brief 析构函数
     * 
     * 清理MyInfo类资源
     * 
     * @note 析构函数是线程安全的
     */
    ~MyInfo();
    
    /**
     * @brief 初始化信息模块
     * 
     * 加载并存储所有预定义信息
     * 初始化过程包括：
     * 1. 检查是否已经初始化，避免重复初始化
     * 2. 清空现有信息
     * 3. 加载信息1（温工程师信息）
     * 4. 加载信息2（粤嵌公司信息）
     * 5. 加载信息3（林院信息）
     * 6. 加载信息4（嵌入式与AI结合的发展前景）
     * 7. 更新初始化状态
     * 
     * @return MyInfoError 初始化结果
     *         - MyInfoError::SUCCESS：初始化成功
     *         - MyInfoError::INIT_FAILED：初始化失败
     * 
     * @note 如果已经初始化，会返回MyInfoError::SUCCESS并输出警告日志
     * @note 建议在系统启动时尽早调用此方法
     * 
     * @warning 不要在多线程环境中同时调用此方法
     * 
     * @see SetPlayVoiceCallback()
     * @see ProcessVoiceCommand()
     */
    MyInfoError Initialize();
    
    /**
     * @brief 设置语音播放回调函数
     * 
     * 设置语音播放回调函数，用于将文本转换为语音并播放
     * 回调函数接收一个字符串参数，表示要播放的文本
     * 
     * @param callback 语音播放回调函数
     * 
     * @note 回调函数类型为std::function<void(const std::string&)>
     * @note 必须设置回调函数，才能播放语音
     * @note 回调函数应该在Application类中设置，调用音频服务播放语音
     * 
     * @warning 回调函数不能为空，否则语音播放会失败
     * 
     * @see Initialize()
     * @see PlayInfo()
     * @see PlayAllInfo()
     * @see TextToSpeech()
     */
    void SetPlayVoiceCallback(PlayVoiceCallback callback);
    
    /**
     * @brief 处理语音指令
     * 
     * 解析语音命令并执行相应的播报操作
     * 支持的语音命令：
     * - "介绍温工"、"温工是谁"、"info1"：播报信息1
     * - "介绍粤嵌公司"、"粤嵌公司"、"info2"：播报信息2
     * - "介绍林院"、"林院是谁"、"info3"：播报信息3
     * - "介绍嵌入式与AI结合的发展前景"、"嵌入式与AI结合的发展前景"、"边缘智能"、"info4"：播报信息4
     * - "所有信息"、"播报所有信息"、"allinfo"：播报所有信息
     * 
     * @param command 语音命令字符串
     * @return MyInfoError 处理结果
     *         - MyInfoError::SUCCESS：处理成功
     *         - MyInfoError::INIT_FAILED：模块未初始化
     *         - MyInfoError::INVALID_COMMAND：无效的命令
     *         - MyInfoError::TTS_FAILED：语音播放失败
     * 
     * @note 如果模块未初始化，会返回MyInfoError::INIT_FAILED
     * @note 如果命令无法识别，会返回MyInfoError::INVALID_COMMAND
     * @note 语音命令解析不区分大小写，自动去除空格
     * 
     * @warning 确保模块已初始化
     * @warning 确保已设置语音播放回调函数
     * 
     * @see ParseVoiceCommand()
     * @see PlayInfo()
     * @see PlayAllInfo()
     */
    MyInfoError ProcessVoiceCommand(const std::string& command);
    
    /**
     * @brief 播报指定信息
     * 
     * 根据索引播报指定信息
     * 索引范围：1、2、3、4
     * 
     * @param index 信息索引（1、2、3、4）
     * @return MyInfoError 播报结果
     *         - MyInfoError::SUCCESS：播报成功
     *         - MyInfoError::INIT_FAILED：模块未初始化
     *         - MyInfoError::INVALID_INDEX：无效的索引
     *         - MyInfoError::INFO_NOT_FOUND：信息未找到
     *         - MyInfoError::TTS_FAILED：语音播放失败
     * 
     * @note 如果模块未初始化，会返回MyInfoError::INIT_FAILED
     * @note 如果索引无效，会返回MyInfoError::INVALID_INDEX
     * @note 如果信息未找到，会返回MyInfoError::INFO_NOT_FOUND
     * @note 如果未设置回调函数，会返回MyInfoError::TTS_FAILED
     * 
     * @warning 确保模块已初始化
     * @warning 确保索引在有效范围内（1-4）
     * @warning 确保已设置语音播放回调函数
     * 
     * @see PlayAllInfo()
     * @see GetInfo()
     */
    MyInfoError PlayInfo(int index);
    
    /**
     * @brief 播报所有信息
     * 
     * 按顺序播报所有信息
     * 播报顺序：信息1 -> 信息2 -> 信息3 -> 信息4
     * 
     * @return MyInfoError 播报结果
     *         - MyInfoError::SUCCESS：播报成功
     *         - MyInfoError::INIT_FAILED：模块未初始化
     *         - MyInfoError::TTS_FAILED：语音播放失败
     * 
     * @note 如果模块未初始化，会返回MyInfoError::INIT_FAILED
     * @note 如果未设置回调函数，会返回MyInfoError::TTS_FAILED
     * @note 播报过程中如果某条信息播放失败，会立即返回错误
     * 
     * @warning 确保模块已初始化
     * @warning 确保已设置语音播放回调函数
     * 
     * @see PlayInfo()
     * @see GetInfoCount()
     */
    MyInfoError PlayAllInfo();
    
    /**
     * @brief 获取指定信息内容
     * 
     * 根据索引获取指定信息内容
     * 索引范围：1、2、3、4
     * 
     * @param index 信息索引（1、2、3、4）
     * @param info 输出参数，信息内容
     * @return MyInfoError 获取结果
     *         - MyInfoError::SUCCESS：获取成功
     *         - MyInfoError::INIT_FAILED：模块未初始化
     *         - MyInfoError::INVALID_INDEX：无效的索引
     *         - MyInfoError::INFO_NOT_FOUND：信息未找到
     * 
     * @note 如果模块未初始化，会返回MyInfoError::INIT_FAILED
     * @note 如果索引无效，会返回MyInfoError::INVALID_INDEX
     * @note 如果信息未找到，会返回MyInfoError::INFO_NOT_FOUND
     * 
     * @warning 确保模块已初始化
     * @warning 确保索引在有效范围内（1-4）
     * 
     * @see GetInfoById()
     * @see GetInfoCount()
     */
    MyInfoError GetInfo(int index, InfoEntry& info);
    
    /**
     * @brief 获取指定信息内容（通过标识符）
     * 
     * 根据ID获取指定信息内容
     * ID范围："info1"、"info2"、"info3"、"info4"
     * 
     * @param id 信息标识符
     * @param info 输出参数，信息内容
     * @return MyInfoError 获取结果
     *         - MyInfoError::SUCCESS：获取成功
     *         - MyInfoError::INIT_FAILED：模块未初始化
     *         - MyInfoError::INFO_NOT_FOUND：信息未找到
     * 
     * @note 如果模块未初始化，会返回MyInfoError::INIT_FAILED
     * @note 如果信息未找到，会返回MyInfoError::INFO_NOT_FOUND
     * 
     * @warning 确保模块已初始化
     * @warning 确保ID有效（"info1"、"info2"、"info3"、"info4"）
     * 
     * @see GetInfo()
     * @see GetInfoCount()
     */
    MyInfoError GetInfoById(const std::string& id, InfoEntry& info);
    
    /**
     * @brief 获取所有信息数量
     * 
     * 返回存储的所有信息数量
     * 
     * @return int 信息数量
     * 
     * @note 该方法不会检查初始化状态，始终返回当前信息数量
     * @note 该方法是线程安全的，可以在多线程环境中调用
     * 
     * @see GetInfo()
     * @see GetInfoById()
     */
    int GetInfoCount() const;
    
    /**
     * @brief 文本转语音
     * 
     * 将文本信息转换为可播放的语音
     * 
     * @param text 文本内容
     * @return MyInfoError 转换结果
     *         - MyInfoError::SUCCESS：转换成功
     *         - MyInfoError::INIT_FAILED：模块未初始化
     *         - MyInfoError::TTS_FAILED：语音播放失败
     * 
     * @note 如果模块未初始化，会返回MyInfoError::INIT_FAILED
     * @note 如果文本为空，会返回MyInfoError::TTS_FAILED
     * @note 如果未设置回调函数，会返回MyInfoError::TTS_FAILED
     * 
     * @warning 确保模块已初始化
     * @warning 确保文本不为空
     * @warning 确保已设置语音播放回调函数
     * 
     * @see SetPlayVoiceCallback()
     * @see PlayInfo()
     * @see PlayAllInfo()
     */
    MyInfoError TextToSpeech(const std::string& text);
};

#endif // MYINFO_H
