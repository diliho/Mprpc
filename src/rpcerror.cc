#include "rpcerror.h"
#include <sstream>
#include <iomanip>
#include <ctime>

// ErrorCode类的实现
std::string ErrorCode::toString() const {
    std::ostringstream oss;
    oss << m_main_code << "-" << std::setw(2) << std::setfill('0') << m_sub_code << "-" << m_module_id;
    return oss.str();
}

// RpcError类的实现
RpcError::RpcError(const ErrorCode& error_code, const std::string& error_msg, 
                 const std::string& node_ip, int node_port, 
                 const std::string& trace_id)
    : m_error_code(error_code), m_error_msg(error_msg),
      m_node_ip(node_ip), m_node_port(node_port), m_trace_id(trace_id),
      m_timestamp(generateTimestamp()) {}

std::string RpcError::toString() const {
    std::ostringstream oss;
    oss << "[RpcError]" << std::endl;
    oss << "  ErrorCode: " << m_error_code.toString() << std::endl;
    oss << "  ErrorMsg: " << m_error_msg << std::endl;
    oss << "  Node: " << m_node_ip << ":" << m_node_port << std::endl;
    oss << "  TraceId: " << m_trace_id << std::endl;
    oss << "  Timestamp: " << m_timestamp << std::endl;
    if (!m_stack_summary.empty()) {
        oss << "  StackSummary: " << m_stack_summary << std::endl;
    }
    return oss.str();
}

std::string RpcError::generateTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&now_time_t), "%Y-%m-%d %H:%M:%S")
        << "." << std::setw(3) << std::setfill('0') << now_ms.count();
    
    return oss.str();
}

// RpcErrorUtil命名空间的实现
ErrorCode RpcErrorUtil::createFrameError(FrameErrorCode code, int sub_code, const std::string& module_id) {
    return ErrorCode(static_cast<int>(code), sub_code, module_id);
}

ErrorCode RpcErrorUtil::createSystemError(SystemErrorCode code, int sub_code, const std::string& module_id) {
    return ErrorCode(static_cast<int>(code), sub_code, module_id);
}

ErrorCode RpcErrorUtil::createBusinessError(BusinessErrorCode code, int sub_code, const std::string& module_id) {
    return ErrorCode(static_cast<int>(code), sub_code, module_id);
}

std::string RpcErrorUtil::getDefaultErrorMessage(const ErrorCode& error_code) {
    int main_code = error_code.getMainCode();
    std::string module_id = error_code.getModuleId();
    
    // 根据主错误码返回默认错误信息
    switch (main_code) {
        case 1001: return "RPC调用超时";
        case 1002: return "序列化失败";
        case 1003: return "反序列化失败";
        case 1004: return "服务发现失败";
        case 1005: return "服务注册失败";
        case 1006: return "心跳检测超时";
        case 1007: return "网络连接失败";
        case 1008: return "无效的RPC头";
        case 1009: return "消息发送失败";
        case 1010: return "消息接收失败";
        case 2001: return "节点负载过高";
        case 2002: return "内存不足";
        case 2003: return "CPU过载";
        case 2004: return "进程崩溃";
        case 2005: return "磁盘空间不足";
        case 2006: return "文件系统错误";
        case 2007: return "系统资源限制";
        case 3001: return "参数校验失败";
        case 3002: return "权限不足";
        case 3003: return "业务规则不满足";
        case 3004: return "资源不存在";
        case 3005: return "数据冲突";
        case 3006: return "操作不支持";
        case 3007: return "业务处理超时";
        default: return "未知错误";
    }
}
