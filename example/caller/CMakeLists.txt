set(SRC_LIST calluserservice.cc ../user.pb.cc)
 add_executable(consumer ${SRC_LIST})
 target_link_libraries(consumer mprpc protobuf)

set(SRC_LIST_FRIEND callfriendservice.cc ../friend.pb.cc ../user.pb.cc)
 add_executable(consumer_friend ${SRC_LIST_FRIEND})
 target_link_libraries(consumer_friend mprpc protobuf)

# QPS测试程序
 set(QPS_TEST_SRC_LIST qps_test.cc ../user.pb.cc)
 add_executable(qps_test ${QPS_TEST_SRC_LIST})
 target_link_libraries(qps_test mprpc protobuf pthread)