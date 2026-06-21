#!/usr/bin/env python3
"""Interactive serial helper based on pexpect."""

from __future__ import annotations

import shlex
import sys
from typing import Optional

import pexpect


class PexpectSerialTerminal:
    """Use picocom (Linux) or direct serial (Windows) for interactive login."""

    def __init__(self, port: str, baudrate: int = 115200, timeout: int = 20) -> None:
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self._child: Optional[pexpect.spawn] = None
        self._use_picocom = sys.platform.startswith("linux")

    def open(self) -> None:
        """Open serial connection."""
        if self._use_picocom:
            # Linux: use picocom
            cmd = f"picocom -b {self.baudrate} {shlex.quote(self.port)}"
            self._child = pexpect.spawn(cmd, encoding="utf-8", timeout=self.timeout)
            self._child.expect([r"Terminal ready", r"FATAL", pexpect.TIMEOUT])
        else:
            # Windows: use direct serial connection via pyserial
            # Note: pexpect on Windows has limitations, fallback to serial.Serial
            import serial
            self._ser = serial.Serial(self.port, self.baudrate, timeout=1)
            self._ser.write(b"\r")
            self._ser.flush()

    def login(self, username: str, password: str, prompt: str = r"[#\$] ") -> str:
        """Perform login with username and password."""
        self._require()
        
        if self._use_picocom:
            # Linux picocom mode
            self._child.sendline("")
            self._child.expect([r"login:", prompt], timeout=self.timeout)
            if "login:" in self._child.after:
                self._child.sendline(username)
                idx = self._child.expect([r"Password:", prompt], timeout=self.timeout)
                if idx == 0:
                    self._child.sendline(password)
                    self._child.expect(prompt, timeout=self.timeout)
            return self._child.before or ""
        else:
            # Windows direct serial mode
            import time
            import re
            
            # Send newline to trigger prompt
            self._ser.write(b"\r")
            self._ser.flush()
            time.sleep(0.5)
            
            # Read buffer
            buf = ""
            deadline = time.time() + self.timeout
            
            while time.time() < deadline:
                if self._ser.in_waiting > 0:
                    data = self._ser.read(self._ser.in_waiting).decode(errors="ignore")
                    buf += data
                    
                    # Detect login prompt
                    if "login:" in buf:
                        self._ser.write(username.encode() + b"\r")
                        self._ser.flush()
                        time.sleep(0.3)
                        buf = ""
                        
                    # Detect password prompt
                    if "Password:" in buf:
                        self._ser.write(password.encode() + b"\r")
                        self._ser.flush()
                        time.sleep(0.3)
                        buf = ""
                        
                    # Detect shell prompt
                    if re.search(prompt, buf):
                        return buf
                        
                time.sleep(0.05)
                
            raise RuntimeError("Login timeout")

    def run(self, command: str, prompt: str = r"[#\$] ") -> str:
        """Run command and wait for output."""
        self._require()
        
        if self._use_picocom:
            # Linux picocom mode
            self._child.sendline(command)
            self._child.expect(prompt, timeout=self.timeout)
            return self._child.before or ""
        else:
            # Windows direct serial mode
            import time
            import re
            
            self._ser.write(command.encode() + b"\r")
            self._ser.flush()
            
            buf = ""
            deadline = time.time() + self.timeout
            
            while time.time() < deadline:
                if self._ser.in_waiting > 0:
                    data = self._ser.read(self._ser.in_waiting).decode(errors="ignore")
                    buf += data
                    
                    # Detect shell prompt (command finished)
                    if re.search(prompt, buf):
                        return buf
                        
                time.sleep(0.05)
                
            raise RuntimeError("Command timeout")

    def close(self) -> None:
        """Close serial connection."""
        if self._use_picocom and self._child is not None:
            self._child.sendcontrol("a")
            self._child.sendcontrol("x")
            self._child.close(force=True)
            self._child = None
        elif hasattr(self, "_ser") and self._ser is not None:
            self._ser.close()
            self._ser = None

    def _require(self) -> None:
        """Check if terminal is opened."""
        if self._use_picocom and self._child is None:
            raise RuntimeError("Terminal not opened")
        if not self._use_picocom and not hasattr(self, "_ser"):
            raise RuntimeError("Serial port not opened")