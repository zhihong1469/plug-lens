#!/usr/bin/env python3
"""Serial port discovery module for auto-detection."""

from __future__ import annotations

import os
from typing import List

from .serial_core import list_serial_ports_detail

# Common USB serial port prefixes (Linux)
USB_SERIAL_PREFIXES_LINUX = ("/dev/ttyACM", "/dev/ttyUSB")

# Common USB serial port prefixes (Windows)
USB_SERIAL_PREFIXES_WINDOWS = ("COM",)


def list_candidate_ports() -> List[str]:
    """List candidate serial ports for embedded boards."""
    ports = []
    for item in list_serial_ports_detail():
        dev = item.get("device", "")
        # Linux USB serial ports
        if any(dev.startswith(prefix) for prefix in USB_SERIAL_PREFIXES_LINUX):
            ports.append(dev)
        # Windows COM ports
        elif dev.startswith("COM"):
            ports.append(dev)
    return ports


def resolve_port(requested_port: str = "") -> str:
    """Resolve serial port from request or auto-detect."""
    # If requested port exists, use it directly
    if requested_port and os.path.exists(requested_port):
        return requested_port

    # If requested port is Windows COM port, check if it's in candidate list
    if requested_port and requested_port.startswith("COM"):
        ports = list_candidate_ports()
        if requested_port in ports:
            return requested_port

    # Auto-detect from candidate ports
    ports = list_candidate_ports()
    if requested_port and requested_port in ports:
        return requested_port
    if ports:
        return ports[0]
    raise RuntimeError("No serial port found. Please check USB connection or specify port manually.")


def get_port_info(port: str) -> dict:
    """Get detailed information of a specific port."""
    for item in list_serial_ports_detail():
        if item.get("device") == port:
            return item
    return {}