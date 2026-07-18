#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "mprpcapplication.h"
#include "mprpcconfig.h"
#include "mprpccontroller.h"
#include "mprpcchannel.h"
#include "mprpcprovider.h"
#include "zookeeperutil.h"
#include "rpcerror.h"
#include "logger.h"
#include <vector>
#include <string>

namespace py = pybind11;

// Simple config wrapper
class PyRpcConfig {
public:
    void load(const std::string& config_file) {
        m_config.LoadConfigfile(config_file.c_str());
    }
    std::string get(const std::string& key) {
        return m_config.Load(key);
    }
    void set(const std::string& key, const std::string& value) {
        m_config.SetConfig(key, value);
    }
    std::string operator[](const std::string& key) {
        return m_config.Load(key);
    }
private:
    MprpcConfig m_config;
};

// Simple controller wrapper
class PyRpcController {
public:
    bool failed() const { return m_controller.Failed(); }
    std::string error_text() const { return m_controller.ErrorText(); }
    void set_failed(const std::string& reason) {
        m_controller.SetFailed(reason);
    }
    MprpcController& raw() { return m_controller; }
private:
    MprpcController m_controller;
};

// ZKClient wrapper
class PyZKClient {
public:
    bool start() {
        auto zk = MprpcApplication::GetZKClient();
        if (zk && zk->isConnected()) {
            m_zk = zk;
            return true;
        }
        return false;
    }
    std::string get_data(const std::string& path) {
        if (!m_zk || !m_zk->isConnected()) return "";
        return m_zk->GetData(path.c_str());
    }
    bool is_connected() {
        return m_zk && m_zk->isConnected();
    }
private:
    std::shared_ptr<ZKClient> m_zk;
};

PYBIND11_MODULE(mprpc_core, m) {
    m.doc() = "Mprpc Python SDK - High-performance C++ RPC framework bindings";
    m.attr("__version__") = "2.0.0";

    // Init function
    m.def("init", [](const std::string& config_file) {
        std::vector<std::string> args = {"mprpc", "-i", config_file};
        std::vector<char*> c_args;
        for (auto& s : args) c_args.push_back(const_cast<char*>(s.c_str()));
        MprpcApplication::Init(static_cast<int>(c_args.size()), c_args.data());
    }, "Initialize Mprpc framework from config file",
       py::arg("config_file"));

    m.def("get_config", []() {
        auto& cfg = MprpcApplication::GetConfig();
        py::dict d;
        std::vector<std::string> keys = {
            "rpcserverip", "rpcserverport",
            "zookeeperip", "zookeeperport",
            "redisip", "redisport"
        };
        for (auto& k : keys) {
            std::string v = cfg.Load(k);
            if (!v.empty()) d[py::str(k)] = py::str(v);
        }
        return d;
    }, "Get current configuration as dict");

    // RpcConfig
    py::class_<PyRpcConfig>(m, "RpcConfig")
        .def(py::init<>())
        .def("load", &PyRpcConfig::load, py::arg("config_file"))
        .def("get", &PyRpcConfig::get, py::arg("key"))
        .def("set", &PyRpcConfig::set, py::arg("key"), py::arg("value"))
        .def("__getitem__", &PyRpcConfig::operator[], py::arg("key"));

    // RpcController
    py::class_<PyRpcController>(m, "RpcController")
        .def(py::init<>())
        .def("failed", &PyRpcController::failed)
        .def("error_text", &PyRpcController::error_text)
        .def("set_failed", &PyRpcController::set_failed, py::arg("reason"));

    // ZKClient
    py::class_<PyZKClient>(m, "ZKClient")
        .def(py::init<>())
        .def("start", &PyZKClient::start)
        .def("get_data", &PyZKClient::get_data, py::arg("path"))
        .def("is_connected", &PyZKClient::is_connected);
}
