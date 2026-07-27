/*
RPC服务提供者主入口
同时提供userservice和friendservice
*/
#include <iostream>
#include "mprpcapplication.h"
#include "mprpcprovider.h"
#include "user.pb.h"
#include "friend.pb.h"
#include "userservice.h"
#include "friendservice.h"
#include "prometheus/metrics_exporter.h"

int main(int argc, char **argv)
{
    MprpcApplication::Init(argc, argv);

    // Start Prometheus metrics exporter
    std::string metrics_port_str = MprpcApplication::GetConfig().Load("metricsexporterport");
    if (!metrics_port_str.empty()) {
        int metrics_port = std::stoi(metrics_port_str);
        mprpc::MetricsExporter::GetInstance().Start(metrics_port);
        std::cout << "Metrics exporter started on port " << metrics_port << std::endl;
    }

    RpcProvider provider;
    provider.NotifyService(new UserService());
    provider.NotifyService(new FriendService());
    provider.Run();
    
    return 0;
}