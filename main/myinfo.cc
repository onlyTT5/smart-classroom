/**
 * @file myinfo.cc
 * @brief MyInfo模块实现文件，提供信息存储和语音播报功能
 * 
 * 本文件实现了MyInfo类，用于存储信息并通过语音命令触发播报。
 * 该模块支持MCP协议，可以通过语音指令播报指定信息或所有信息。
 * 
 * @author 粤嵌.温工
 * @date 2026-02-24
 * @version 1.0.0
 * 
 * @section 实现说明
 * 
 * 1. 信息存储：
 *    - 使用std::vector<InfoEntry>存储所有预定义信息
 *    - 每条信息包含ID、标题、内容和索引
 *    - 信息在初始化时加载到内存中
 * 
 * 2. 语音命令解析：
 *    - 使用字符串匹配解析语音命令
 *    - 支持多种命令格式，提高识别准确性
 *    - 不区分大小写，自动去除空格
 * 
 * 3. 语音播放：
 *    - 使用回调函数机制实现语音播放
 *    - 回调函数由Application类设置，调用音频服务播放语音
 *    - 支持单条信息播放和所有信息播放
 * 
 * 
 * @section 性能说明
 * 
 * - 信息存储：内存中存储，访问速度快
 * - 语音命令解析：字符串匹配，响应时间毫秒级
 * - 语音播放：取决于语音引擎性能
 * - 内存占用：约1KB（三条信息）
 * 
 * @section 调试说明
 * 
 * - 使用ESP_LOGI()输出操作日志
 * - 使用ESP_LOGE()输出错误日志
 * - 使用ESP_LOGW()输出警告日志
 * - 日志标签为"MYINFO"
 * - 可通过menuconfig调整日志级别
 */

#include "myinfo.h"
#include "application.h"
#include <esp_log.h>
#include <algorithm>
#include <cctype>

/**
 * @brief 日志标签，用于标识MyInfo模块的日志输出
 * 
 * 该标签用于所有MyInfo模块相关的日志输出
 * 可通过menuconfig调整日志级别
 * 
 * @note 日志标签应简短且具有描述性
 * @note 建议使用大写字母和下划线
 */
static const char* TAG = "MYINFO";

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
MyInfo::MyInfo() : initialized_(false) {
    ESP_LOGI(TAG, "MyInfo构造函数调用");
}

/**
 * @brief 析构函数
 * 
 * 清理MyInfo类资源
 * 
 * @note 析构函数是线程安全的
 */
MyInfo::~MyInfo() {
    ESP_LOGI(TAG, "MyInfo析构函数调用");
}

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
 * 6. 更新初始化状态
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
MyInfoError MyInfo::Initialize() {
    if (initialized_) {
        ESP_LOGW(TAG, "MyInfo已经初始化，跳过重复初始化");
        return MyInfoError::SUCCESS;
    }

    ESP_LOGI(TAG, "开始初始化MyInfo模块");

    // 清空现有信息，确保没有残留数据
    info_entries_.clear();

    // 加载信息1：温工程师信息
    InfoEntry info1;
    info1.id = "info1";  // 信息唯一标识符
    info1.title = "介绍温工";  // 信息标题
    info1.content = "温工程师 - 粤嵌科技技术专家，10年+嵌入式开发经验，ESP32物联网专家，专长：嵌入式系统、AI语音、工业物联网，代表项目：智能语音助手、工业物联网网关";  // 信息内容
    info1.index = 1;  // 信息索引
    info_entries_.push_back(info1);  // 添加到信息列表
    ESP_LOGI(TAG, "加载信息1成功: %s", info1.title.c_str());

    // 加载信息2：粤嵌公司信息
    InfoEntry info2;
    info2.id = "info2";  // 信息唯一标识符
    info2.title = "介绍粤嵌公司";  // 信息标题
    info2.content = "广州粤嵌通信科技股份有限公司，国家级高新技术企业，成立于2005年，核心业务：嵌入式培训、物联网研发、AI解决方案，官网：www.yueqian.com.cn";  // 信息内容
    info2.index = 2;  // 信息索引
    info_entries_.push_back(info2);  // 添加到信息列表
    ESP_LOGI(TAG, "加载信息2成功: %s", info2.title.c_str());

    // 加载信息3：林院信息
    InfoEntry info3;
    info3.id = "info3";  // 信息唯一标识符
    info3.title = "介绍林院";  // 信息标题
    info3.content = "林院，特级讲师，视觉设计类教学专家，有逾十年的设计教学经验，在视觉设计闭环式教学上有自己独到的一套方法论与教育哲学。对商业目标和用户行为具有前瞻性思考，能够在复杂业务场景中洞察本质，持续稳定输出优秀设计方案，对设计结果呈现能精准预判。";  // 信息内容
    info3.index = 3;  // 信息索引
    info_entries_.push_back(info3);  // 添加到信息列表
    ESP_LOGI(TAG, "加载信息3成功: %s", info3.title.c_str());

    // 加载信息4：嵌入式与AI结合的发展前景
    InfoEntry info4;
    info4.id = "info4";  // 信息唯一标识符
    info4.title = "介绍嵌入式与AI结合的发展前景";  // 信息标题
    info4.content = "嵌入式加AI（边缘智能）前景广阔。行业需求旺盛，智能硬件市场庞大，人才缺口显著。掌握AI部署、模型优化、RISC-V及异构计算技能的复合型工程师将处于高薪、无35岁危机的黄金赛道。建议从基础扎实，快速切入边缘AI项目实践。";  // 信息内容
    info4.index = 4;  // 信息索引
    info_entries_.push_back(info4);  // 添加到信息列表
    ESP_LOGI(TAG, "加载信息4成功: %s", info4.title.c_str());

    // 更新初始化状态
    initialized_ = true;
    ESP_LOGI(TAG, "MyInfo初始化成功，共加载%d条信息", info_entries_.size());
    return MyInfoError::SUCCESS;
}

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
void MyInfo::SetPlayVoiceCallback(PlayVoiceCallback callback) {
    ESP_LOGI(TAG, "设置语音播放回调函数");
    play_voice_callback_ = callback;
}

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
VoiceCommand MyInfo::ParseVoiceCommand(const std::string& command) {
    ESP_LOGI(TAG, "解析语音命令: %s", command.c_str());

    // 转换为小写以便匹配，提高命令识别的准确性
    std::string cmd_lower = command;
    std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(), ::tolower);

    // 去除空格，避免空格影响命令识别
    cmd_lower.erase(std::remove_if(cmd_lower.begin(), cmd_lower.end(), ::isspace), cmd_lower.end());

    ESP_LOGI(TAG, "处理后的命令: %s", cmd_lower.c_str());

    // 匹配命令，支持多种命令格式
    if (cmd_lower == "介绍温工" || cmd_lower == "温工是谁" || cmd_lower == "info1") {
        ESP_LOGI(TAG, "识别到命令: 播报信息1");
        return VoiceCommand::PLAY_INFO_1;
    } else if (cmd_lower == "介绍粤嵌公司" || cmd_lower == "粤嵌公司" || cmd_lower == "info2") {
        ESP_LOGI(TAG, "识别到命令: 播报信息2");
        return VoiceCommand::PLAY_INFO_2;
    } else if (cmd_lower == "介绍林院" || cmd_lower == "林院是谁" || cmd_lower == "info3") {
        ESP_LOGI(TAG, "识别到命令: 播报信息3");
        return VoiceCommand::PLAY_INFO_3;
    } else if (cmd_lower == "介绍嵌入式与AI结合的发展前景" || cmd_lower == "嵌入式与AI结合的发展前景" || cmd_lower == "边缘智能" || cmd_lower == "info4") {
        ESP_LOGI(TAG, "识别到命令: 播报信息4");
        return VoiceCommand::PLAY_INFO_4;
    } else if (cmd_lower == "所有信息" || cmd_lower == "播报所有信息" || cmd_lower == "allinfo") {
        ESP_LOGI(TAG, "识别到命令: 播报所有信息");
        return VoiceCommand::PLAY_ALL_INFO;
    } else {
        ESP_LOGW(TAG, "无法识别的命令: %s", command.c_str());
        return VoiceCommand::UNKNOWN_COMMAND;
    }
}

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
std::string MyInfo::GetErrorMessage(MyInfoError error) {
    switch (error) {
        case MyInfoError::SUCCESS:
            return "操作成功";
        case MyInfoError::INFO_NOT_FOUND:
            return "信息未找到";
        case MyInfoError::INVALID_INDEX:
            return "无效的信息索引";
        case MyInfoError::TTS_FAILED:
            return "语音合成失败";
        case MyInfoError::INIT_FAILED:
            return "初始化失败";
        case MyInfoError::INVALID_COMMAND:
            return "无效的命令";
        default:
            return "未知错误";
    }
}

/**
 * @brief 处理语音指令
 * 
 * 解析语音命令并执行相应的播报操作
 * 支持的语音命令：
 * - "介绍温工"、"温工是谁"、"info1"：播报信息1
 * - "介绍粤嵌公司"、"粤嵌公司"、"info2"：播报信息2
 * - "介绍林院"、"林院是谁"、"info3"：播报信息3
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
MyInfoError MyInfo::ProcessVoiceCommand(const std::string& command) {
    if (!initialized_) {
        ESP_LOGE(TAG, "MyInfo未初始化，无法处理语音命令");
        return MyInfoError::INIT_FAILED;
    }

    ESP_LOGI(TAG, "处理语音命令: %s", command.c_str());

    // 解析语音命令
    VoiceCommand cmd = ParseVoiceCommand(command);

    // 根据命令类型执行相应的操作
    switch (cmd) {
        case VoiceCommand::PLAY_INFO_1:
            ESP_LOGI(TAG, "执行命令: 播报信息1");
            return PlayInfo(1);
        case VoiceCommand::PLAY_INFO_2:
            ESP_LOGI(TAG, "执行命令: 播报信息2");
            return PlayInfo(2);
        case VoiceCommand::PLAY_INFO_3:
            ESP_LOGI(TAG, "执行命令: 播报信息3");
            return PlayInfo(3);
        case VoiceCommand::PLAY_INFO_4:
            ESP_LOGI(TAG, "执行命令: 播报信息4");
            return PlayInfo(4);
        case VoiceCommand::PLAY_ALL_INFO:
            ESP_LOGI(TAG, "执行命令: 播报所有信息");
            return PlayAllInfo();
        case VoiceCommand::UNKNOWN_COMMAND:
            ESP_LOGW(TAG, "未知的语音命令: %s", command.c_str());
            return MyInfoError::INVALID_COMMAND;
        default:
            ESP_LOGW(TAG, "无效的命令类型");
            return MyInfoError::INVALID_COMMAND;
    }
}

/**
 * @brief 播报指定信息
 * 
 * 根据索引播报指定信息
 * 索引范围：1、2、3
 * 
 * @param index 信息索引（1、2、3）
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
 * @warning 确保索引在有效范围内（1-3）
 * @warning 确保已设置语音播放回调函数
 * 
 * @see PlayAllInfo()
 * @see GetInfo()
 */
MyInfoError MyInfo::PlayInfo(int index) {
    if (!initialized_) {
        ESP_LOGE(TAG, "MyInfo未初始化，无法播报信息");
        return MyInfoError::INIT_FAILED;
    }

    // 检查索引是否有效
    if (index < 1 || index > static_cast<int>(info_entries_.size())) {
        ESP_LOGE(TAG, "无效的信息索引: %d", index);
        return MyInfoError::INVALID_INDEX;
    }

    // 获取指定信息
    InfoEntry info;
    MyInfoError result = GetInfo(index, info);
    if (result != MyInfoError::SUCCESS) {
        ESP_LOGE(TAG, "获取信息失败，索引: %d", index);
        return result;
    }

    ESP_LOGI(TAG, "播报信息%d: %s", index, info.content.c_str());

    // 调用语音播放回调函数
    if (play_voice_callback_) {
        play_voice_callback_(info.content);
        ESP_LOGI(TAG, "信息播报成功");
        return MyInfoError::SUCCESS;
    } else {
        ESP_LOGW(TAG, "语音播放回调函数未设置");
        return MyInfoError::TTS_FAILED;
    }
}

/**
 * @brief 播报所有信息
 * 
 * 按顺序播报所有信息
 * 播报顺序：信息1 -> 信息2 -> 信息3
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
MyInfoError MyInfo::PlayAllInfo() {
    if (!initialized_) {
        ESP_LOGE(TAG, "MyInfo未初始化，无法播报所有信息");
        return MyInfoError::INIT_FAILED;
    }

    ESP_LOGI(TAG, "开始播报所有信息，共%d条", info_entries_.size());

    // 按顺序播报所有信息
    for (const auto& info : info_entries_) {
        if (play_voice_callback_) {
            ESP_LOGI(TAG, "播报信息%d: %s", info.index, info.content.c_str());
            play_voice_callback_(info.content);
        } else {
            ESP_LOGW(TAG, "语音播放回调函数未设置");
            return MyInfoError::TTS_FAILED;
        }
    }

    ESP_LOGI(TAG, "所有信息播报完成");
    return MyInfoError::SUCCESS;
}

/**
 * @brief 获取指定信息内容
 * 
 * 根据索引获取指定信息内容
 * 索引范围：1、2、3
 * 
 * @param index 信息索引（1、2、3）
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
 * @warning 确保索引在有效范围内（1-3）
 * 
 * @see GetInfoById()
 * @see GetInfoCount()
 */
MyInfoError MyInfo::GetInfo(int index, InfoEntry& info) {
    if (!initialized_) {
        ESP_LOGE(TAG, "MyInfo未初始化，无法获取信息");
        return MyInfoError::INIT_FAILED;
    }

    // 检查索引是否有效
    if (index < 1 || index > static_cast<int>(info_entries_.size())) {
        ESP_LOGE(TAG, "无效的信息索引: %d", index);
        return MyInfoError::INVALID_INDEX;
    }

    // 查找对应的信息（索引从1开始）
    for (const auto& entry : info_entries_) {
        if (entry.index == index) {
            info = entry;
            ESP_LOGI(TAG, "获取信息%d成功: %s", index, info.content.c_str());
            return MyInfoError::SUCCESS;
        }
    }

    ESP_LOGE(TAG, "未找到索引为%d的信息", index);
    return MyInfoError::INFO_NOT_FOUND;
}

/**
 * @brief 获取指定信息内容（通过标识符）
 * 
 * 根据ID获取指定信息内容
 * ID范围："info1"、"info2"、"info3"
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
 * @warning 确保ID有效（"info1"、"info2"、"info3"）
 * 
 * @see GetInfo()
 * @see GetInfoCount()
 */
MyInfoError MyInfo::GetInfoById(const std::string& id, InfoEntry& info) {
    if (!initialized_) {
        ESP_LOGE(TAG, "MyInfo未初始化，无法获取信息");
        return MyInfoError::INIT_FAILED;
    }

    ESP_LOGI(TAG, "通过ID获取信息: %s", id.c_str());

    // 查找对应的信息
    for (const auto& entry : info_entries_) {
        if (entry.id == id) {
            info = entry;
            ESP_LOGI(TAG, "获取信息成功（ID: %s）: %s", id.c_str(), info.content.c_str());
            return MyInfoError::SUCCESS;
        }
    }

    ESP_LOGE(TAG, "未找到ID为%s的信息", id.c_str());
    return MyInfoError::INFO_NOT_FOUND;
}

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
int MyInfo::GetInfoCount() const {
    int count = static_cast<int>(info_entries_.size());
    ESP_LOGI(TAG, "当前信息数量: %d", count);
    return count;
}

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
MyInfoError MyInfo::TextToSpeech(const std::string& text) {
    if (!initialized_) {
        ESP_LOGE(TAG, "MyInfo未初始化，无法进行文本转语音");
        return MyInfoError::INIT_FAILED;
    }

    // 检查文本是否为空
    if (text.empty()) {
        ESP_LOGW(TAG, "文本为空，无法进行文本转语音");
        return MyInfoError::TTS_FAILED;
    }

    ESP_LOGI(TAG, "文本转语音: %s", text.c_str());

    // 调用语音播放回调函数
    if (play_voice_callback_) {
        play_voice_callback_(text);
        ESP_LOGI(TAG, "文本转语音成功");
        return MyInfoError::SUCCESS;
    } else {
        ESP_LOGW(TAG, "语音播放回调函数未设置");
        return MyInfoError::TTS_FAILED;
    }
}
