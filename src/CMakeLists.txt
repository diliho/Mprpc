# 显式列出所有源文件
set(SRC_LIST
    Logger.cc
    mprpcapplication.cc
    mprpcchannel.cc
    mprpcconfig.cc
    mprpccontroller.cc
    mprpcprovider.cc
    rpcerror.cc
    rpcheader.pb.cc
    zookeeperutil.cc
)

# 添加头文件搜索路径
include_directories(./include)
# 添加muduo库的头文件搜索路径
include_directories(/usr/local/include)

# 将共享库改为静态库
add_library(mprpc STATIC ${SRC_LIST})

link_libraries(muduo_net muduo_base pthread)
target_link_libraries(mprpc muduo_net muduo_base pthread zookeeper_mt)