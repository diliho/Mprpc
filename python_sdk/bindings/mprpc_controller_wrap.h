#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "mprpccontroller.h"
#include "rpcerror.h"

namespace py = pybind11;

class PyRpcController {
public:
    PyRpcController() = default;

    bool failed() const { return m_controller.Failed(); }

    std::string error_text() const { return m_controller.ErrorText(); }

    void set_failed(const std::string& reason) {
        m_controller.SetFailed(reason);
    }

    MprpcController& raw() { return m_controller; }
    const MprpcController& raw() const { return m_controller; }

private:
    MprpcController m_controller;
};

class PyRpcError {
public:
    PyRpcError(const RpcError& err) : m_error(err) {}

    std::string error_code() const { return m_error.getErrorCode().toString(); }
    int main_code() const { return m_error.getErrorCode().getMainCode(); }
    std::string error_msg() const { return m_error.getErrorMsg(); }
    std::string node_ip() const { return m_error.getNodeIp(); }
    int node_port() const { return m_error.getNodePort(); }
    std::string trace_id() const { return m_error.getTraceId(); }
    std::string timestamp() const { return m_error.getTimestamp(); }
    std::string to_string() const { return m_error.toString(); }

    std::string repr() const {
        return "<RpcError " + m_error.toString() + ">";
    }

private:
    RpcError m_error;
};

inline void bind_controller(py::module_& m) {
    py::class_<PyRpcError>(m, "RpcError")
        .def("error_code", &PyRpcError::error_code)
        .def("main_code", &PyRpcError::main_code)
        .def("error_msg", &PyRpcError::error_msg)
        .def("node_ip", &PyRpcError::node_ip)
        .def("node_port", &PyRpcError::node_port)
        .def("trace_id", &PyRpcError::trace_id)
        .def("timestamp", &PyRpcError::timestamp)
        .def("__str__", &PyRpcError::to_string)
        .def("__repr__", &PyRpcError::repr);

    py::class_<PyRpcController>(m, "RpcController")
        .def(py::init<>())
        .def("failed", &PyRpcController::failed, "Check if call failed")
        .def("error_text", &PyRpcController::error_text, "Get error text")
        .def("set_failed", &PyRpcController::set_failed,
             "Set error state", py::arg("reason"));
}
