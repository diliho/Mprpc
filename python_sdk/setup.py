from setuptools import setup, find_packages

setup(
    name="mprpc",
    version="1.0.0",
    description="Mprpc Python SDK — pure Python RPC client compatible with C++ Mprpc providers",
    packages=find_packages(),
    package_data={"mprpc": ["rpcheader_pb2.py"]},
    install_requires=[
        "protobuf>=3.12",
        "kazoo>=2.8",
    ],
    python_requires=">=3.8",
    author="Mprpc-EE",
    license="MIT",
)
