#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>
#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "mprpcapplication.h"
#include "rpcerror.h"
#include <thread>
#include <future>

namespace py = pybind11;

class PyRpcChannel : public MprpcChannel {
public:
    using MprpcChannel::MprpcChannel;

    void CallMethod(const google::protobuf::MethodDescriptor* method,
                    google::protobuf::RpcController* controller,
                    const google::protobuf::Message* request,
                    google::protobuf::Message* response,
                    google::protobuf::Closure* done) override {
        PYBIND11_OVERRIDE_PURE(
            void,
            MprpcChannel,
            CallMethod,
            method, controller, request, response, done
        );
    }
};

class RpcChannelWrapper {
public:
    RpcChannelWrapper() {
        m_channel = std::make_unique<MprpcChannel>();
    }

    ~RpcChannelWrapper() = default;

    std::tuple<py::object, py::object> call_method_sync(
        const std::string& service_name,
        const std::string& method_name,
        const py::bytes& request_data,
        const std::string& response_type_name) {

        auto* stub_class = google::protobuf::DescriptorPool::generated_pool()
            ->FindMessageTypeByName(response_type_name);
        if (!stub_class) {
            throw py::value_error("Unknown message type: " + response_type_name);
        }

        google::protobuf::Message* response =
            google::protobuf::MessageFactory::generated_factory()
                ->GetPrototype(stub_class)->New();
        if (!response) {
            throw py::runtime_error("Failed to create response message");
        }

        std::string req_str(request_data);
        if (!response->ParseFromString(req_str)) {
            delete response;
            throw py::runtime_error("Failed to parse response");
        }

        MprpcController controller;

        m_channel->CallMethod(nullptr, &controller, nullptr, response, nullptr);

        py::gil_scoped_acquire acquire;
        if (controller.Failed()) {
            py::object err = py::cast(PyRpcError(
                RpcError(RpcErrorUtil::createFrameError(
                    FrameErrorCode::RPC_CALL_TIMEOUT, 0),
                    controller.ErrorText())));
            return py::make_tuple(py::none(), err);
        }

        std::string resp_str;
        response->SerializeToString(&resp_str);
        delete response;

        py::bytes resp_bytes(resp_str);
        return py::make_tuple(resp_bytes, py::none());
    }

    MprpcChannel* raw() { return m_channel.get(); }

private:
    std::unique_ptr<MprpcChannel> m_channel;
};

inline void bind_channel(py::module_& m) {
    py::class_<PyRpcChannel, MprpcChannel, PyRpcChannel, std::unique_ptr<PyRpcChannel>>(m, "RpcChannelBase")
        .def(py::init<>())
        .def("call_method", &PyRpcChannel::CallMethod);

    py::class_<RpcChannelWrapper>(m, "RpcChannel")
        .def(py::init<>())
        .def("call_method_sync", &RpcChannelWrapper::call_method_sync,
             "Synchronous RPC call",
             py::arg("service_name"),
             py::arg("method_name"),
             py::arg("request_data"),
             py::arg("response_type_name"));
}
