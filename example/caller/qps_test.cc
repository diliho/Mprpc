#include<iostream>
#include<thread>
#include<vector>
#include<atomic>
#include<chrono>
#include"mprpcapplication.h"
#include"user.pb.h"
#include"mprpcchannel.h"
#include"zookeeperutil.h"

// QPS测试参数
const int THREAD_NUM = 10;        // 线程数
const int TEST_DURATION = 5;      // 测试持续时间(秒)

// 全局统计变量
std::atomic<int64_t> total_requests(0);
std::atomic<int64_t> success_requests(0);
std::atomic<int64_t> failed_requests(0);

// 全局共享的MprpcChannel实例
MprpcChannel* g_channel = nullptr;

// 线程函数：持续调用rpc方法
void test_rpc() {
    // 所有线程共享同一个channel
    fixbug::UserServiceRpc_Stub stub(g_channel);
    
    // 创建请求参数
    fixbug::LoginRequest request;
    request.set_name("test");
    request.set_pwd("123456");
    
    // 创建响应对象
    fixbug::LoginResponse response;
    
    // 记录开始时间
    auto start_time = std::chrono::steady_clock::now();
    
    while (true) {
        // 检查是否超时
        auto current_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
        if (duration >= TEST_DURATION) {
            break;
        }
        
        // 增加总请求数
        total_requests++;
        
        // 调用rpc方法
        stub.Login(nullptr, &request, &response, nullptr);
        
        // 检查调用结果
        if (!response.result().errorcode()) {
            success_requests++;
        } else {
            failed_requests++;
        }
    }
}

int main(int argc, char **argv) {
    // 初始化mprpc框架
    MprpcApplication::Init(argc, argv);
    
    // 创建全局共享的channel实例
    g_channel = new MprpcChannel();
    
    // 创建线程池
    std::vector<std::thread> threads;
    
    // 记录测试开始时间
    auto test_start = std::chrono::steady_clock::now();
    
    // 启动多个线程
    for (int i = 0; i < THREAD_NUM; ++i) {
        threads.emplace_back(test_rpc);
    }
    
    // 等待所有线程完成
    for (auto& thread : threads) {
        thread.join();
    }
    
    // 记录测试结束时间
    auto test_end = std::chrono::steady_clock::now();
    auto actual_duration = std::chrono::duration_cast<std::chrono::seconds>(test_end - test_start).count();
    
    // 计算QPS
    double qps = static_cast<double>(total_requests) / actual_duration;
    
    // 输出统计结果
    std::cout << "QPS Test Result:" << std::endl;
    std::cout << "====================" << std::endl;
    std::cout << "Thread Num: " << THREAD_NUM << std::endl;
    std::cout << "Test Duration: " << actual_duration << " seconds" << std::endl;
    std::cout << "Total Requests: " << total_requests << std::endl;
    std::cout << "Success Requests: " << success_requests << std::endl;
    std::cout << "Failed Requests: " << failed_requests << std::endl;
    std::cout << "QPS: " << qps << std::endl;
    std::cout << "Success Rate: " << (static_cast<double>(success_requests) / total_requests) * 100 << "%" << std::endl;
    
    // 释放全局channel资源
    delete g_channel;
    
    return 0;
}