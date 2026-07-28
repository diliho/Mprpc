"""RPC config loader — parses key=value .conf files."""


class RpcConfig:
    """Load and query RPC configuration from a .conf file.

    File format: one key=value pair per line, # for comments.
    """

    def __init__(self):
        self._data: dict[str, str] = {}

    def load(self, filepath: str):
        """Load a configuration file."""
        with open(filepath, "r") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                if "=" in line:
                    key, _, value = line.partition("=")
                    self._data[key.strip()] = value.strip()

    def get(self, key: str) -> str:
        """Get a config value, empty string if not found."""
        return self._data.get(key, "")

    def get_or(self, key: str, default: str) -> str:
        """Get a config value with a default."""
        return self._data.get(key, default)

    def __repr__(self):
        return f"RpcConfig({self._data})"
