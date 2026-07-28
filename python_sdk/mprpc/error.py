"""RPC error classes."""


class RpcError(Exception):
    """RPC call error with structured error info."""

    def __init__(self, message: str, code: int = 0, node_ip: str = "", node_port: int = 0):
        self.code = code
        self.message = message
        self.node_ip = node_ip
        self.node_port = node_port
        super().__init__(f"[{code}] {message}" + (f" @ {node_ip}:{node_port}" if node_ip else ""))
