#include<iostream>
#include<thread>
#include<vector>
#include<atomic>
#include<chrono>
#include<unistd.h>
#include"mprpcapplication.h"
#include"user.pb.h"
#include"mprpcchannel.h"
#include"mprpccontroller.h"
// QPS测试参数
const int THREAD_NUM = 8;        // 线程数（充分利用多核）
const int TEST_DURATION = 5;      // 测试持续时间(秒)
const int WARMUP_CALLS = 10;      // 预发请求数（建立连接+填充缓存）

// 全局统计变量
std::atomic<int64_t> total_requests(0);
std::atomic<int64_t> success_requests(0);
std::atomic<int64_t> failed_requests(0);

// 线程函数：持续调用rpc方法（每个线程独立channel，因为MprpcChannel非线程安全）
void test_rpc() {
    MprpcChannel channel;
    fixbug::UserServiceRpc_Stub stub(&channel);
    
    // 创建请求参数
    fixbug::LoginRequest request;
    request.set_name("test");
    request.set_pwd("123456");
    
    // Warm-up: 发送预发请求，建立ZK缓存和TCP长连接
    for (int i = 0; i < WARMUP_CALLS; i++) {
        MprpcController controller;
        fixbug::LoginResponse resp;
        stub.Login(&controller, &request, &resp, nullptr);
    }
    
    // 记录开始时间
    auto start_time = std::chrono::steady_clock::now();
    
    while (true) {
        auto current_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
        if (duration >= TEST_DURATION) break;
        
        total_requests++;
        
        MprpcController controller;
        fixbug::LoginResponse resp;
        stub.Login(&controller, &request, &resp, nullptr);
        
        if (!controller.Failed() && !resp.result().errorcode())
            success_requests++;
        else {
            if (failed_requests == 0)
                std::cout << "first error: " << controller.ErrorText() << std::endl;
            failed_requests++;
        }
    }
}

int main(int argc, char **argv) {
    // 初始化mprpc框架
    MprpcApplication::Init(argc, argv);

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
    std::cout << "\nQPS Test Result:" << std::endl;
    std::cout << "====================" << std::endl;
    std::cout << "Thread Num: " << THREAD_NUM << std::endl;
    std::cout << "Test Duration: " << actual_duration << " seconds" << std::endl;
    std::cout << "Total Requests: " << total_requests << std::endl;
    std::cout << "Success Requests: " << success_requests << std::endl;
    std::cout << "Failed Requests: " << failed_requests << std::endl;
    std::cout << "QPS: " << qps << std::endl;
    std::cout << "Success Rate: " << (static_cast<double>(success_requests) / total_requests) * 100 << "%" << std::endl;

    // Logger后台线程阻塞在条件变量上，正常退出会因静态析构顺序导致死锁
    _exit(0);
}