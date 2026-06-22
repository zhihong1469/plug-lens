#!/usr/bin/env python3
"""Standalone serial communication CLI tool."""

from __future__ import annotations

import argparse
import os
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional

try:
    import serial
    from serial import SerialException
    from serial.tools import list_ports
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial", file=sys.stderr)
    sys.exit(1)


@dataclass
class SerialConfig:
    port: str
    baudrate: int = 115200
    timeout: float = 1.0
    write_timeout: float = 1.0


class SerialSession:
    def __init__(self, cfg: SerialConfig) -> None:
        self.cfg = cfg
        self._ser: Optional[serial.Serial] = None

    def open(self) -> None:
        if self._ser and self._ser.is_open:
            return
        self._ser = serial.Serial(
            port=self.cfg.port,
            baudrate=self.cfg.baudrate,
            timeout=self.cfg.timeout,
            write_timeout=self.cfg.write_timeout,
        )

    def close(self) -> None:
        if self._ser and self._ser.is_open:
            self._ser.close()

    def write_line(self, text: str, line_end: str = "\r") -> None:
        self._require_open()
        payload = (text + line_end).encode("utf-8", errors="ignore")
        self._ser.write(payload)
        self._ser.flush()

    def read_until_quiet(self, quiet_sec: float = 0.3, max_sec: float = 3.0) -> str:
        self._require_open()
        end_deadline = time.time() + max_sec
        last_data_ts = time.time()
        chunks: List[bytes] = []
        while time.time() < end_deadline:
            waiting = self._ser.in_waiting
            if waiting > 0:
                chunks.append(self._ser.read(waiting))
                last_data_ts = time.time()
            elif time.time() - last_data_ts >= quiet_sec:
                break
            else:
                time.sleep(0.02)
        return b"".join(chunks).decode("utf-8", errors="replace")

    def _require_open(self) -> None:
        if not self._ser or not self._ser.is_open:
            raise SerialException("Serial port is not open")


def list_serial_ports_detail() -> List[Dict[str, str]]:
    items: List[Dict[str, str]] = []
    for p in sorted(list_ports.comports(), key=lambda x: x.device):
        vid = f"0x{p.vid:04x}" if p.vid is not None else ""
        pid = f"0x{p.pid:04x}" if p.pid is not None else ""
        items.append({
            "device": p.device or "",
            "name": p.name or "",
            "description": p.description or "",
            "hwid": p.hwid or "",
            "vid": vid,
            "pid": pid,
            "serial_number": p.serial_number or "",
            "manufacturer": p.manufacturer or "",
            "product": p.product or "",
        })
    return items


def list_candidate_ports() -> List[str]:
    ports = []
    prefixes = ("/dev/ttyACM", "/dev/ttyUSB", "COM")
    for item in list_serial_ports_detail():
        dev = item.get("device", "")
        if any(dev.startswith(p) for p in prefixes):
            ports.append(dev)
    return ports


def resolve_port(requested_port: str = "") -> str:
    if requested_port and os.path.exists(requested_port):
        return requested_port
    
    if requested_port and requested_port.startswith("COM"):
        ports = list_candidate_ports()
        if requested_port in ports:
            return requested_port
    
    ports = list_candidate_ports()
    if requested_port and requested_port in ports:
        return requested_port
    if ports:
        return ports[0]
    raise RuntimeError("No serial port found. Please check USB connection.")


SHELL_PROMPT_RE = re.compile(r"[#\$] $", re.M)


def run_login_check(
    port: str = "",
    baudrate: int = 115200,
    username: str = "",
    password: str = "",
    cmd: str = "uname -a",
    wait_sec: int = 120,
    attempt_interval_sec: float = 2.0,
) -> int:
    def _log_action(msg):
        print(f"[serial-check] {msg}")
    
    def _write_line(ser, text):
        ser.write(text.encode() + b"\r")
        ser.flush()

    deadline = time.monotonic() + wait_sec
    
    while True:
        ser = None
        try:
            active_port = resolve_port(port)
            _log_action(f"selected port: {active_port}")
            ser = serial.Serial(active_port, baudrate, timeout=0.2, write_timeout=1)
            buf = ""
            sent_user = False
            sent_cmd = False
            next_ping = 0.0
            
            while time.monotonic() < deadline:
                now = time.monotonic()
                if now >= next_ping:
                    _log_action("send <CR>")
                    _write_line(ser, "")
                    next_ping = now + 3.0
                
                data = ser.read(4096)
                if data:
                    text = data.decode(errors="ignore")
                    print(text, end="")
                    buf = (buf + text)[-50000:]
                    
                    if "login:" in buf and not sent_user:
                        user = username if username else "root"
                        _log_action(f"detected login prompt -> send {user}")
                        _write_line(ser, user)
                        sent_user = True
                        buf = ""
                        next_ping = time.monotonic() + 1.0
                        continue
                    
                    if SHELL_PROMPT_RE.search(buf) and not sent_cmd:
                        _log_action(f"detected shell prompt -> send command: {cmd}")
                        _write_line(ser, cmd)
                        sent_cmd = True
                        buf = ""
                        next_ping = time.monotonic() + 1.0
                        continue
                    
                    if sent_cmd and SHELL_PROMPT_RE.search(buf):
                        return 0
                        
                time.sleep(0.05)
            
        except (RuntimeError, OSError, serial.SerialException) as exc:
            last_error = str(exc)
            print(f"[serial-check] error: {last_error}")
        finally:
            if ser:
                ser.close()

        if time.monotonic() >= deadline:
            print("ERROR: timeout", file=sys.stderr)
            return 1
        time.sleep(attempt_interval_sec)


def cmd_list_ports():
    ports = list_serial_ports_detail()
    if not ports:
        print("No serial ports found")
        return 1

    print("Available serial ports:")
    print("-" * 80)
    for p in ports:
        print(f"Device: {p['device']}")
        print(f"  Description: {p['description']}")
        print(f"  Manufacturer: {p['manufacturer']}")
        print(f"  Product: {p['product']}")
        print(f"  VID/PID: {p['vid']}/{p['pid']}")
        print(f"  Serial Number: {p['serial_number']}")
        print()
    return 0


def cmd_login_check(args):
    return run_login_check(
        port=args.port,
        baudrate=args.baudrate,
        username=args.username,
        password=args.password,
        cmd=args.cmd,
        wait_sec=args.wait_sec,
        attempt_interval_sec=args.attempt_interval_sec,
    )


def build_parser():
    parser = argparse.ArgumentParser(description="Serial communication CLI")
    sub = parser.add_subparsers(dest="subcmd", required=True)

    sub.add_parser("list-ports", help="List all available serial ports")

    login = sub.add_parser("login-check", help="Login and run verification command")
    login.add_argument("--port", default="", help="Serial port path")
    login.add_argument("--baudrate", type=int, default=115200, help="Baud rate")
    login.add_argument("--username", default="", help="Login username")
    login.add_argument("--password", default="", help="Login password")
    login.add_argument("--cmd", default="uname -a", help="Command to execute")
    login.add_argument("--wait-sec", type=int, default=120, help="Max wait time")
    login.add_argument("--attempt-interval-sec", type=float, default=2.0, help="Retry interval")

    return parser


def main():
    parser = build_parser()
    args = parser.parse_args()

    if args.subcmd == "list-ports":
        return cmd_list_ports()
    elif args.subcmd == "login-check":
        return cmd_login_check(args)
    else:
        parser.error(f"Unknown command: {args.subcmd}")
        return 2


if __name__ == "__main__":
    sys.exit(main())