# 搜索当前目录和include目录的源文件
aux_source_directory(. SRC_LIST)
aux_source_directory(./include SRC_LIST)

# 添加头文件搜索路径
include_directories(./include)
# 添加muduo库的头文件搜索路径
include_directories(/usr/local/include)

# 将共享库改为静态库
add_library(mprpc STATIC ${SRC_LIST})

link_libraries(muduo_net muduo_base pthread)
target_link_libraries(mprpc muduo_net muduo_base pthread zookeeper_mt)