#!/usr/bin/env python3
"""Automatic login and command execution module for embedded Linux boards."""

from __future__ import annotations

import re
import sys
import time
from pathlib import Path

import serial

try:
    from .discovery import resolve_port
except ImportError:
    from discovery import resolve_port


SHELL_PROMPT_RE = re.compile(r"[#\$] $", re.M)


def _write_line(ser: serial.Serial, text: str) -> None:
    """Write a line to serial port with CR ending."""
    ser.write(text.encode() + b"\r")
    ser.flush()


def _log_action(message: str) -> None:
    """Log action message."""
    print(f"[serial-check] {message}")


def run_non_login(
    port: str,
    baudrate: int,
    cmd: str,
    wait_sec: int,
    attempt_interval_sec: float,
) -> int:
    """
    Run command without explicit login (for boards with auto-login or no password).
    
    This function will:
    1. Detect login prompt and send 'root' if needed
    2. Wait for shell prompt
    3. Execute verification command
    4. Return exit code
    """
    deadline = time.monotonic() + wait_sec
    last_error = "serial command did not succeed"

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
                    
                    # Detect login prompt
                    if "login:" in buf and not sent_user:
                        _log_action("detected login prompt -> send root")
                        _write_line(ser, "root")
                        sent_user = True
                        buf = ""
                        next_ping = time.monotonic() + 1.0
                        continue
                    
                    # Detect shell prompt
                    if SHELL_PROMPT_RE.search(buf) and not sent_cmd:
                        _log_action(f"detected shell prompt -> send command: {cmd}")
                        _write_line(ser, cmd)
                        sent_cmd = True
                        buf = ""
                        next_ping = time.monotonic() + 1.0
                        continue
                    
                    # Command execution completed
                    if sent_cmd and SHELL_PROMPT_RE.search(buf):
                        return 0
                        
                time.sleep(0.05)
            last_error = "timed out waiting for login prompt or command output"
            
        except (RuntimeError, OSError, serial.SerialException) as exc:
            last_error = str(exc)
        finally:
            if ser is not None:
                ser.close()

        if time.monotonic() >= deadline:
            print(f"ERROR: {last_error}", file=sys.stderr)
            return 1
        time.sleep(attempt_interval_sec)


def run_login(
    port: str,
    baudrate: int,
    user: str,
    password: str,
    cmd: str,
    wait_sec: int,
    attempt_interval_sec: float,
) -> int:
    """
    Run command with explicit login (for boards requiring password).
    
    This function uses pexpect for more reliable login handling.
    """
    try:
        import pexpect
    except Exception:
        print("ERROR: pexpect not installed, run: pip install pexpect", file=sys.stderr)
        return 2

    # Check for picocom on Linux
    if sys.platform.startswith("linux"):
        if not Path("/usr/bin/picocom").exists():
            print("ERROR: picocom not installed, run: sudo apt-get install -y picocom", file=sys.stderr)
            return 2

    deadline = time.monotonic() + wait_sec
    last_error = "serial login timed out"

    while True:
        term = None
        try:
            active_port = resolve_port(port)
            print(f"[serial-check] selected port: {active_port}")
            
            # Use pexpect for login
            try:
                from .serial_pexpect import PexpectSerialTerminal
            except ImportError:
                from serial_pexpect import PexpectSerialTerminal
            
            term = PexpectSerialTerminal(port=active_port, baudrate=baudrate, timeout=30)
            term.open()
            term.login(user, password)
            output = term.run(cmd)
            print(output)
            return 0
            
        except (pexpect.TIMEOUT, pexpect.EOF, OSError, RuntimeError) as exc:
            last_error = str(exc)
        finally:
            if term is not None:
                term.close()

        if time.monotonic() >= deadline:
            print(f"ERROR: {last_error}", file=sys.stderr)
            return 1
        time.sleep(attempt_interval_sec)


def run_login_check(
    port: str = "",
    baudrate: int = 115200,
    username: str = "",
    password: str = "",
    cmd: str = "uname -a",
    wait_sec: int = 120,
    attempt_interval_sec: float = 2.0,
) -> int:
    """
    Main entry point for serial login and command execution.
    
    Args:
        port: Serial port path (auto-detect if empty)
        baudrate: Baud rate (default 115200)
        username: Login username (use non-login mode if empty)
        password: Login password
        cmd: Command to execute after login
        wait_sec: Maximum wait time in seconds
        attempt_interval_sec: Interval between retry attempts
    
    Returns:
        0 on success, 1 on failure, 2 on missing dependencies
    """
    if username:
        return run_login(
            port=port,
            baudrate=baudrate,
            user=username,
            password=password,
            cmd=cmd,
            wait_sec=wait_sec,
            attempt_interval_sec=attempt_interval_sec,
        )
    return run_non_login(
        port=port,
        baudrate=baudrate,
        cmd=cmd,
        wait_sec=wait_sec,
        attempt_interval_sec=attempt_interval_sec,
    )