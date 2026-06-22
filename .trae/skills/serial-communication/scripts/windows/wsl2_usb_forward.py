#!/usr/bin/env python3
"""WSL2 USB Serial Port Forwarding Tool.

This tool helps manage USB serial port forwarding from Windows to WSL2,
automatically detecting and forwarding the correct device based on VID/PID.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from typing import List, Optional

# Common USB Serial VID/PID pairs
KNOWN_DEVICES = {
    "ch340": {"vid": "1a86", "pid": "7523", "name": "USB-SERIAL CH340"},
    "pl2303": {"vid": "067b", "pid": "2303", "name": "Prolific USB-to-Serial"},
    "ftdi": {"vid": "0403", "pid": "6001", "name": "FTDI USB-to-Serial"},
    "cp210x": {"vid": "10c4", "pid": "ea60", "name": "Silicon Labs CP210x"},
}


def get_windows_usb_devices() -> List[dict]:
    """Get USB devices list from Windows using usbipd."""
    try:
        result = subprocess.run(
            ["usbipd", "list"],
            capture_output=True,
            check=True
        )
        # Try different encodings for Windows
        for encoding in ["utf-8", "gbk", "cp936"]:
            try:
                output = result.stdout.decode(encoding)
                break
            except UnicodeDecodeError:
                continue
        else:
            output = result.stdout.decode("utf-8", errors="ignore")
        
        lines = output.strip().split("\n")
        devices = []
        
        for line in lines[2:]:  # Skip header lines
            parts = line.split()
            if len(parts) < 5:
                continue
            
            busid = parts[0]
            vid_pid = parts[1]
            state = parts[-1]
            
            # Extract VID/PID
            if ":" in vid_pid:
                vid, pid = vid_pid.split(":")
            else:
                vid, pid = "", ""
            
            # Get device name (everything between VID/PID and STATE)
            name = " ".join(parts[2:-1])
            
            devices.append({
                "busid": busid,
                "vid": vid.lower(),
                "pid": pid.lower(),
                "name": name,
                "state": state,
            })
        
        return devices
    except subprocess.CalledProcessError:
        print("ERROR: usbipd command failed. Is usbipd installed?", file=sys.stderr)
        return []
    except FileNotFoundError:
        print("ERROR: usbipd not found. Install via: winget install usbipd", file=sys.stderr)
        return []


def find_device_by_vid_pid(vid: str, pid: str) -> Optional[dict]:
    """Find USB device by VID/PID."""
    devices = get_windows_usb_devices()
    for dev in devices:
        if dev["vid"] == vid.lower() and dev["pid"] == pid.lower():
            return dev
    return None


def find_device_by_name(name: str) -> Optional[dict]:
    """Find USB device by name pattern."""
    devices = get_windows_usb_devices()
    for dev in devices:
        if name.lower() in dev["name"].lower():
            return dev
    return None


def bind_device(busid: str) -> bool:
    """Bind USB device for sharing."""
    try:
        result = subprocess.run(
            ["usbipd", "bind", "--busid", busid],
            capture_output=True,
            text=True,
            check=True
        )
        print(f"Bound device {busid}")
        print(result.stdout)
        return True
    except subprocess.CalledProcessError as e:
        # Check if already bound
        if "already bound" in e.stderr.lower():
            print(f"Device {busid} is already bound")
            return True
        # Check if access denied
        if "access denied" in e.stderr.lower() or "administrator" in e.stderr.lower():
            print("ERROR: Administrator privileges required!")
            print("Please run this script in an elevated PowerShell/Command Prompt.")
            print("Or run the following command manually as administrator:")
            print(f"  usbipd bind --busid {busid}")
            print(f"  usbipd attach --busid {busid} --wsl")
        else:
            print(f"ERROR: Failed to bind device: {e.stderr}", file=sys.stderr)
        return False


def forward_device(busid: str) -> bool:
    """Forward USB device to WSL2."""
    # First try to bind if needed
    bind_device(busid)
    
    try:
        # New usbipd syntax (without 'wsl' subcommand)
        result = subprocess.run(
            ["usbipd", "attach", "--busid", busid, "--wsl"],
            capture_output=True,
            text=True,
            check=True
        )
        print(f"Forwarded device {busid} to WSL2")
        print(result.stdout)
        return True
    except subprocess.CalledProcessError as e:
        print(f"ERROR: Failed to forward device: {e.stderr}", file=sys.stderr)
        return False


def detach_device(busid: str) -> bool:
    """Detach USB device from WSL2."""
    try:
        # New usbipd syntax (without 'wsl' subcommand)
        result = subprocess.run(
            ["usbipd", "detach", "--busid", busid],
            capture_output=True,
            text=True,
            check=True
        )
        print(f"Detached device {busid} from WSL2")
        return True
    except subprocess.CalledProcessError as e:
        print(f"ERROR: Failed to detach device: {e.stderr}", file=sys.stderr)
        return False


def get_wsl_serial_port(vid: str, pid: str) -> Optional[str]:
    """Get the serial port path in WSL2 after forwarding."""
    # Common WSL2 serial port paths
    port_patterns = [
        f"/dev/serial/by-id/usb-{KNOWN_DEVICES.get('ch340', {}).get('name', 'unknown')}-if00-port0",
        f"/dev/serial/by-id/usb-*-if00-port0",
        "/dev/ttyUSB0",
        "/dev/ttyACM0",
    ]
    
    # Check if any of these paths exist
    for pattern in port_patterns:
        if "*" in pattern:
            # Simple glob-like matching
            import glob
            matches = glob.glob(pattern)
            if matches:
                return matches[0]
        elif os.path.exists(pattern):
            return pattern
    
    return None


def cmd_list_devices():
    """List all USB devices."""
    devices = get_windows_usb_devices()
    if not devices:
        print("No USB devices found")
        return 1
    
    print("USB Devices:")
    print("-" * 80)
    for dev in devices:
        print(f"BUSID: {dev['busid']}")
        print(f"  VID/PID: {dev['vid']}:{dev['pid']}")
        print(f"  Name: {dev['name']}")
        print(f"  State: {dev['state']}")
        print()
    return 0


def cmd_forward(args):
    """Forward USB device to WSL2."""
    # Find device
    device = None
    
    if args.vid and args.pid:
        device = find_device_by_vid_pid(args.vid, args.pid)
    elif args.name:
        device = find_device_by_name(args.name)
    elif args.busid:
        devices = get_windows_usb_devices()
        device = next((d for d in devices if d["busid"] == args.busid), None)
    
    if not device:
        print("ERROR: Device not found", file=sys.stderr)
        return 1
    
    print(f"Found device: {device['name']} (BUSID: {device['busid']})")
    
    # Detach if already attached
    if device["state"] != "Not shared":
        print(f"Device is currently {device['state']}, detaching first...")
        detach_device(device["busid"])
    
    # Forward to WSL2
    if forward_device(device["busid"]):
        print("\nDevice forwarded successfully!")
        print("In WSL2, the serial port is typically:")
        print("  /dev/ttyUSB0 or /dev/ttyACM0")
        return 0
    return 1


def cmd_auto_forward(args):
    """Auto-detect and forward serial device."""
    # Try known serial devices
    for device_type, info in KNOWN_DEVICES.items():
        device = find_device_by_vid_pid(info["vid"], info["pid"])
        if device:
            print(f"Found {info['name']}: BUSID={device['busid']}")
            return cmd_forward(argparse.Namespace(
                vid=info["vid"],
                pid=info["pid"],
                name=None,
                busid=None
            ))
    
    # Try to find any serial device by name patterns
    serial_patterns = ["serial", "COM", "UART", "CH340", "CP210", "PL2303", "FTDI"]
    devices = get_windows_usb_devices()
    
    for dev in devices:
        if any(pattern.lower() in dev["name"].lower() for pattern in serial_patterns):
            print(f"Found serial device: {dev['name']} (BUSID: {dev['busid']})")
            return cmd_forward(argparse.Namespace(
                vid=None,
                pid=None,
                name=None,
                busid=dev["busid"]
            ))
    
    print("ERROR: No serial device found", file=sys.stderr)
    return 1


def build_parser():
    parser = argparse.ArgumentParser(description="WSL2 USB Serial Forwarding Tool")
    sub = parser.add_subparsers(dest="subcmd", required=True)
    
    sub.add_parser("list", help="List all USB devices")
    
    forward = sub.add_parser("forward", help="Forward USB device to WSL2")
    forward.add_argument("--busid", help="USB BUSID (e.g., 3-3)")
    forward.add_argument("--vid", help="Vendor ID (e.g., 1a86)")
    forward.add_argument("--pid", help="Product ID (e.g., 7523)")
    forward.add_argument("--name", help="Device name pattern")
    
    sub.add_parser("auto", help="Auto-detect and forward serial device")
    
    detach = sub.add_parser("detach", help="Detach USB device from WSL2")
    detach.add_argument("--busid", required=True, help="USB BUSID")
    
    return parser


def main():
    parser = build_parser()
    args = parser.parse_args()
    
    if args.subcmd == "list":
        return cmd_list_devices()
    elif args.subcmd == "forward":
        return cmd_forward(args)
    elif args.subcmd == "auto":
        return cmd_auto_forward(args)
    elif args.subcmd == "detach":
        return detach_device(args.busid)
    else:
        parser.error(f"Unknown command: {args.subcmd}")
        return 2


if __name__ == "__main__":
    sys.exit(main())