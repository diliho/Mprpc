/*
错误码分层分类：
    按 “异常来源” 拆分为三层，便于精准定位问题：
    框架层错误（1xxx）：RPC 框架自身的异常，如网络连接超时、序列化 / 反序列化失败、服务注册 / 发现失败、心跳检测异常等；
    系统层错误（2xxx）：服务节点的系统异常，如节点负载过高、资源不足（内存 / CPU）、进程崩溃等；
    业务层错误（3xxx）：业务逻辑异常，如参数校验失败、权限不足、业务规则不满足等。
错误码结构设计：
    采用 “主错误码 + 子错误码 + 模块标识” 的组合，
    例如 “1001（框架层 - 调用超时）+ 01（客户端模块）+ MPRPC-CLIENT”，同时绑定「默认提示信息 + 自定义扩展信息」；
异常信息载体：
    封装统一的RpcError对象，包含错误码、错误描述、异常发生节点 IP / 端口、全局调用 ID（traceId）、异常发生时间戳、可选的堆栈摘要（轻量版，避免性能损耗），确保异常信息能完整传递。
*/




#pragma once
#include <string>
#include <chrono>
#include <memory>

// 错误码分层定义
// 框架层错误（1xxx）：RPC框架自身的异常
enum class FrameErrorCode {
    RPC_CALL_TIMEOUT = 1001,        // RPC调用超时
    SERIALIZE_FAILED = 1002,        // 序列化失败
    DESERIALIZE_FAILED = 1003,      // 反序列化失败
    SERVICE_DISCOVERY_FAILED = 1004, // 服务发现失败
    SERVICE_REGISTER_FAILED = 1005,  // 服务注册失败
    HEARTBEAT_TIMEOUT = 1006,       // 心跳检测超时
    NETWORK_CONNECT_FAILED = 1007,  // 网络连接失败
    INVALID_RPC_HEADER = 1008,      // 无效的RPC头
    MESSAGE_SEND_FAILED = 1009,     // 消息发送失败
    MESSAGE_RECV_FAILED = 1010,     // 消息接收失败
};

// 系统层错误（2xxx）：服务节点的系统异常
enum class SystemErrorCode {
    NODE_OVERLOAD = 2001,           // 节点负载过高
    MEMORY_INSUFFICIENT = 2002,     // 内存不足
    CPU_OVERLOAD = 2003,            // CPU过载
    PROCESS_CRASH = 2004,           // 进程崩溃
    DISK_SPACE_INSUFFICIENT = 2005, // 磁盘空间不足
    FILE_SYSTEM_ERROR = 2006,       // 文件系统错误
    SYSTEM_RESOURCE_LIMIT = 2007,   // 系统资源限制
};

// 业务层错误（3xxx）：业务逻辑异常
enum class BusinessErrorCode {
    PARAMETER_VALIDATION_FAILED = 3001, // 参数校验失败
    PERMISSION_DENIED = 3002,           // 权限不足
    BUSINESS_RULE_VIOLATION = 3003,     // 业务规则不满足
    RESOURCE_NOT_FOUND = 3004,          // 资源不存在
    DATA_CONFLICT = 3005,               // 数据冲突
    OPERATION_NOT_SUPPORTED = 3006,     // 操作不支持
    BUSINESS_TIMEOUT = 3007,            // 业务处理超时
};

// 错误码结构：主错误码 + 子错误码 + 模块标识
class ErrorCode {
public:
    ErrorCode(int main_code, int sub_code, const std::string& module_id)
        : m_main_code(main_code), m_sub_code(sub_code), m_module_id(module_id) {}
    
    // 获取完整错误码字符串，格式：主错误码-子错误码-模块标识
    std::string toString() const;
    
    // 获取主错误码
    int getMainCode() const { return m_main_code; }
    
    // 获取子错误码
    int getSubCode() const { return m_sub_code; }
    
    // 获取模块标识
    std::string getModuleId() const { return m_module_id; }
    
private:
    int m_main_code;       // 主错误码
    int m_sub_code;        // 子错误码
    std::string m_module_id; // 模块标识
};

// 统一的异常信息载体
class RpcError {
public:
    RpcError(const ErrorCode& error_code, const std::string& error_msg, 
             const std::string& node_ip = "", int node_port = 0, 
             const std::string& trace_id = "");
    
    // 获取错误码
    const ErrorCode& getErrorCode() const { return m_error_code; }
    
    // 获取错误描述
    const std::string& getErrorMsg() const { return m_error_msg; }
    
    // 获取异常发生节点IP
    const std::string& getNodeIp() const { return m_node_ip; }
    
    // 获取异常发生节点端口
    int getNodePort() const { return m_node_port; }
    
    // 获取全局调用ID
    const std::string& getTraceId() const { return m_trace_id; }
    
    // 获取异常发生时间戳
    const std::string& getTimestamp() const { return m_timestamp; }
    
    // 获取堆栈摘要
    const std::string& getStackSummary() const { return m_stack_summary; }
    
    // 设置堆栈摘要
    void setStackSummary(const std::string& stack_summary) { m_stack_summary = stack_summary; }
    
    // 获取完整错误信息字符串
    std::string toString() const;
    
private:
    ErrorCode m_error_code;          // 错误码
    std::string m_error_msg;         // 错误描述
    std::string m_node_ip;           // 异常发生节点IP
    int m_node_port;                 // 异常发生节点端口
    std::string m_trace_id;          // 全局调用ID
    std::string m_timestamp;         // 异常发生时间戳
    std::string m_stack_summary;     // 堆栈摘要
    
    // 生成当前时间戳字符串
    static std::string generateTimestamp();
};

// 错误码工具函数
namespace RpcErrorUtil {
    // 创建框架层错误码
    ErrorCode createFrameError(FrameErrorCode code, int sub_code, const std::string& module_id = "MPRPC");
    
    // 创建系统层错误码
    ErrorCode createSystemError(SystemErrorCode code, int sub_code, const std::string& module_id = "MPRPC");
    
    // 创建业务层错误码
    ErrorCode createBusinessError(BusinessErrorCode code, int sub_code, const std::string& module_id = "MPRPC");
    
    // 根据错误码获取默认错误信息
    std::string getDefaultErrorMessage(const ErrorCode& error_code);
}
