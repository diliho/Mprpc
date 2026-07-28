"""RPC controller — tracks success/failure of a single RPC call."""


class RpcController:
    """Tracks the result status of an RPC call.

    Usage:
        ctrl = RpcController()
        channel.call_method(..., controller=ctrl)
        if ctrl.failed():
            print(ctrl.error_text())
    """

    def __init__(self):
        self._failed = False
        self._error_text = ""

    def reset(self):
        """Reset controller to initial state."""
        self._failed = False
        self._error_text = ""

    def failed(self) -> bool:
        """Whether the call failed."""
        return self._failed

    def error_text(self) -> str:
        """The error description string."""
        return self._error_text

    def set_failed(self, reason: str):
        """Mark the call as failed with a reason."""
        self._failed = True
        self._error_text = reason

    def __repr__(self):
        if self._failed:
            return f"RpcController(FAILED: {self._error_text})"
        return "RpcController(OK)"
