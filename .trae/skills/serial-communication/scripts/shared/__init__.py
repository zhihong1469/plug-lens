"""Shared serial communication utilities."""

from .serial_core import (
    SerialConfig,
    SerialSession,
    list_serial_ports,
    list_serial_ports_detail,
    auto_select_serial_port,
    make_serial_config,
)

from .discovery import (
    list_candidate_ports,
    resolve_port,
    get_port_info,
)

from .login_check import (
    run_login_check,
)

__all__ = [
    "SerialConfig",
    "SerialSession",
    "list_serial_ports",
    "list_serial_ports_detail",
    "auto_select_serial_port",
    "make_serial_config",
    "list_candidate_ports",
    "resolve_port",
    "get_port_info",
    "run_login_check",
]