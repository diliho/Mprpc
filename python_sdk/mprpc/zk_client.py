"""ZooKeeper service discovery client using kazoo.

Discovers RPC provider addresses from ZooKeeper sequential ephemeral znodes.
"""

import random
import logging
from typing import Optional, Callable

from kazoo.client import KazooClient

logger = logging.getLogger("mprpc.zk")


class ZkClient:
    """ZooKeeper client for Mprpc service discovery.

    Usage:
        zk = ZkClient("127.0.0.1:2181")
        zk.start()
        addr = zk.get_service_addr("/UserServiceRpc/Login")
        zk.close()
    """

    def __init__(self, connect_str: str = "127.0.0.1:2181", timeout_sec: float = 10.0):
        self._connect_str = connect_str
        self._timeout_sec = timeout_sec
        self._client: Optional[KazooClient] = None

    def start(self) -> bool:
        """Connect to ZooKeeper. Returns True on success."""
        try:
            self._client = KazooClient(
                hosts=self._connect_str,
                timeout=self._timeout_sec,
            )
            self._client.start(timeout=self._timeout_sec)
            logger.info("ZK connected to %s", self._connect_str)
            return True
        except Exception as e:
            logger.error("ZK connect failed: %s", e)
            return False

    def get_children(self, path: str) -> list[str]:
        """List child znodes under path."""
        if not self._client:
            return []
        try:
            children = self._client.get_children(path)
            return children if isinstance(children, list) else list(children)
        except Exception as e:
            logger.debug("ZK get_children(%s) failed: %s", path, e)
            return []

    def get_data(self, path: str) -> Optional[str]:
        """Read znode data as string."""
        if not self._client:
            return None
        try:
            result = self._client.get(path)
            # kazoo returns (data, stat) or (data, stat, header) depending on version
            data = result[0] if isinstance(result, tuple) else result
            if data:
                return data.decode("utf-8")
            return None
        except Exception as e:
            logger.debug("ZK get_data(%s) failed: %s", path, e)
            return None

    def get_service_addr(self, method_path: str) -> Optional[str]:
        """Discover a provider address for the given method path.

        The method_path should be like "/UserServiceRpc/Login".
        Lists children (inst_* znodes), picks a random one, reads its data.

        Returns:
            "ip:port" string or None if discovery fails.
        """
        children = self.get_children(method_path)
        if not children:
            # Fallback: try reading method_path directly (old single-writer format)
            return self.get_data(method_path)

        # Pick a random child for basic load balancing
        chosen = random.choice(children)
        child_path = f"{method_path}/{chosen}"
        return self.get_data(child_path)

    def watch_children(self, path: str, callback: Callable):
        """Register a child watcher on path.

        The callback is called with (children_list) when children change.
        """
        if not self._client:
            return

        def _watcher(children):
            logger.debug("ZK watcher triggered for %s: %s", path, children)
            callback(children)

        self._client.ChildrenWatch(path, _watcher)

    def close(self):
        """Close ZK connection."""
        if self._client:
            try:
                self._client.stop()
                self._client.close()
            except Exception:
                pass
            self._client = None

    @property
    def connected(self) -> bool:
        return self._client is not None and self._client.connected
