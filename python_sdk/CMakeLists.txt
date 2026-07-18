cmake_minimum_required(VERSION 3.14)
project(mprpc_python_sdk VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(pybind11_DIR "/home/dili/.local/lib/python3.10/site-packages/pybind11/share/cmake/pybind11")
find_package(pybind11 REQUIRED)

set(MPRPC_ROOT "${CMAKE_SOURCE_DIR}/..")
set(MPRPC_BUILD "${MPRPC_ROOT}/lib")
set(MPRPC_SRC "${MPRPC_ROOT}/src")

include_directories(
    ${MPRPC_SRC}/include
    ${MPRPC_SRC}/include/net
    ${MPRPC_SRC}/include/registry
    ${MPRPC_SRC}/include/balance
    ${MPRPC_SRC}/include/pool
    ${MPRPC_SRC}/include/prometheus
)

find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
find_package(Threads REQUIRED)

set(MUDUO_INCLUDE_DIR "/home/dili/local/muduo/include")
set(MUDUO_LIBRARY_DIR "/home/dili/local/muduo/lib")
include_directories(${MUDUO_INCLUDE_DIR})

set(ZOOKEEPER_INCLUDE_DIR "/usr/include")
set(ZOOKEEPER_LIBRARY_DIR "/usr/lib/x86_64-linux-gnu")

pybind11_add_module(mprpc_core bindings/mprpc_module.cc)

target_link_libraries(mprpc_core PRIVATE
    ${MPRPC_BUILD}/libmprpc.a
    ${Protobuf_LIBRARIES}
    ${ZOOKEEPER_LIBRARY_DIR}/libzookeeper_mt.so
    hiredis
    ${MUDUO_LIBRARY_DIR}/libmuduo_net.a
    ${MUDUO_LIBRARY_DIR}/libmuduo_base.a
    pthread
    dl
)

target_include_directories(mprpc_core PRIVATE
    ${MPRPC_SRC}/include
    ${MPRPC_SRC}/include/net
    ${MPRPC_SRC}/include/registry
    ${MPRPC_SRC}/include/balance
    ${MPRPC_SRC}/include/pool
    ${MPRPC_SRC}/include/prometheus
    ${ZOOKEEPER_INCLUDE_DIR}
    ${MUDUO_INCLUDE_DIR}
)

target_compile_definitions(mprpc_core PRIVATE
    VERSION_INFO="${PROJECT_VERSION}"
)

install(TARGETS mprpc_core LIBRARY DESTINATION mprpc)
install(FILES mprpc/__init__.py DESTINATION mprpc)
