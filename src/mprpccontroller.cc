#include"mprpccontroller.h"
#include"rpcerror.h"

MprpcController::MprpcController()
{
    m_failed = false;
    m_errText = "";
    m_rpc_error = nullptr;
}

void MprpcController::Reset()
{
    m_failed = false;
    m_errText = "";
    m_rpc_error = nullptr;
}

bool MprpcController::Failed() const
{
    return m_failed;
}

std::string MprpcController::ErrorText() const
{
    if (m_rpc_error) {
        return m_rpc_error->toString();
    }
    return m_errText;
}

void MprpcController::SetFailed(const std::string &reason)
{
    m_failed = true;
    m_errText = reason;
    // 创建一个默认的框架层错误
    auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode::RPC_CALL_TIMEOUT, 0);
    m_rpc_error = std::make_unique<RpcError>(error_code, reason);
}

// 新增的SetFailed方法，支持RpcError
void MprpcController::SetFailed(const RpcError& error)
{
    m_failed = true;
    m_errText = error.getErrorMsg();
    m_rpc_error = std::make_unique<RpcError>(error);
}

// 新增的GetError方法
const RpcError* MprpcController::GetError() const
{
    return m_rpc_error.get();
}

void MprpcController::StartCancel()
{
    
}

bool MprpcController::IsCanceled() const
{
    
    return false;
}

void MprpcController::NotifyOnCancel(google::protobuf::Closure* callback)
{
   
    (void)callback; 
}
