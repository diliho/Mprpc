from setuptools import setup, find_packages
from pybind11.setup_helpers import Pybind11Extension, build_ext
import os

ext_modules = [
    Pybind11Extension(
        "mprpc_core",
        ["bindings/mprpc_module.cc"],
        include_dirs=[
            os.path.join(os.path.dirname(__file__), "..", "src", "include"),
            os.path.join(os.path.dirname(__file__), "..", "src", "include", "net"),
            os.path.join(os.path.dirname(__file__), "..", "src", "include", "registry"),
            os.path.join(os.path.dirname(__file__), "..", "src", "include", "balance"),
            os.path.join(os.path.dirname(__file__), "..", "src", "include", "pool"),
            os.path.join(os.path.dirname(__file__), "..", "src", "include", "prometheus"),
        ],
        libraries=[
            "protobuf", "zookeeper_mt", "hiredis",
            "muduo_net", "muduo_base",
            "pthread",
        ],
        library_dirs=[
            "/usr/lib", "/usr/local/lib",
            os.path.join(os.path.dirname(__file__), "..", "build", "lib"),
        ],
        cxx_std=17,
        define_macros=[("VERSION_INFO", "2.0.0")],
    ),
]

setup(
    name="mprpc",
    version="2.0.0",
    author="Mprpc Team",
    description="High-performance C++ RPC framework Python SDK",
    long_description=open("../README.md").read() if os.path.exists("../README.md") else "",
    long_description_content_type="text/markdown",
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    packages=find_packages(),
    python_requires=">=3.8",
    install_requires=[
        "pybind11>=2.10",
    ],
    classifiers=[
        "Programming Language :: Python :: 3",
        "Programming Language :: C++",
        "License :: OSI Approved :: MIT License",
        "Operating System :: POSIX :: Linux",
    ],
)
