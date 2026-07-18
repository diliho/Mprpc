#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "mprpcapplication.h"
#include "mprpcconfig.h"

namespace py = pybind11;

class PyRpcConfig {
public:
    PyRpcConfig() = default;

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

    MprpcConfig& raw() { return m_config; }

private:
    MprpcConfig m_config;
};

inline void bind_config(py::module_& m) {
    py::class_<PyRpcConfig>(m, "RpcConfig")
        .def(py::init<>())
        .def("load", &PyRpcConfig::load, "Load config from file",
             py::arg("config_file"))
        .def("get", &PyRpcConfig::get, "Get config value by key",
             py::arg("key"))
        .def("set", &PyRpcConfig::set, "Set config value",
             py::arg("key"), py::arg("value"))
        .def("__getitem__", &PyRpcConfig::operator[],
             "Get config value by key (dict-style)");
}
