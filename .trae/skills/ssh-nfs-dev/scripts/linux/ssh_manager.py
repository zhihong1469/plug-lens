#!/usr/bin/env python3
"""SSH connection manager for embedded Linux development boards."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple


@dataclass
class SSHConfig:
    """SSH connection configuration."""
    host: str = "192.168.5.11"
    user: str = "root"
    port: int = 22
    timeout: int = 10
    key_file: str = ""


@dataclass
class NFSConfig:
    """NFS mount configuration."""
    server_ip: str = "192.168.5.10"  # WSL2/Host IP
    mount_point: str = "/mnt/nfs"
    nfs_path: str = "/home/luo/linux/6ull/project/plug-lens"
    mount_cmd_alias: str = "mount_nfs_wired"


class NetworkDetector:
    """Detect network configuration and connectivity."""
    
    @staticmethod
    def get_wsl_ip() -> Optional[str]:
        """Get WSL2 IP address (prefer eth2 for embedded development)."""
        try:
            result = subprocess.run(
                ["ip", "addr", "show"],
                capture_output=True,
                text=True,
                timeout=5
            )
            if result.returncode == 0:
                # Prefer eth2 (embedded development network)
                for line in result.stdout.split("\n"):
                    if "eth2:" in line:
                        # Found eth2 interface, now find its IP
                        eth2_section = result.stdout.split("eth2:")[1].split("\n\n")[0] if "eth2:" in result.stdout else ""
                        match = re.search(r"inet (\d+\.\d+\.\d+\.\d+)", eth2_section)
                        if match:
                            return match.group(1)
                
                # Fallback: look for 192.168.5.x network (embedded dev)
                for line in result.stdout.split("\n"):
                    if "inet 192.168.5." in line:
                        match = re.search(r"inet (\d+\.\d+\.\d+\.\d+)", line)
                        if match:
                            return match.group(1)
                
                # Last fallback: any non-127.0.0.1 IP
                for line in result.stdout.split("\n"):
                    if "inet " in line and not "127.0.0.1" in line and not "10.255.255.254" in line:
                        match = re.search(r"inet (\d+\.\d+\.\d+\.\d+)", line)
                        if match:
                            return match.group(1)
        except Exception:
            pass
        
        # Fallback: try hostname command
        try:
            result = subprocess.run(
                ["hostname", "-I"],
                capture_output=True,
                text=True,
                timeout=5
            )
            if result.returncode == 0:
                ips = result.stdout.strip().split()
                for ip in ips:
                    if ip.startswith("192.168.5."):
                        return ip
                for ip in ips:
                    if not ip.startswith("127.") and not ip.startswith("10.255."):
                        return ip
        except Exception:
            pass
        
        return None
    
    @staticmethod
    def ping_host(host: str, timeout: int = 2) -> bool:
        """Check if host is reachable."""
        try:
            result = subprocess.run(
                ["ping", "-c", "1", "-W", str(timeout), host],
                capture_output=True,
                timeout=timeout + 1
            )
            return result.returncode == 0
        except Exception:
            return False
    
    @staticmethod
    def check_ssh_connection(cfg: SSHConfig) -> Tuple[bool, str]:
        """Check SSH connection status."""
        try:
            cmd = [
                "ssh",
                "-o", f"ConnectTimeout={cfg.timeout}",
                "-o", "StrictHostKeyChecking=no",
                f"{cfg.user}@{cfg.host}",
                "echo 'SSH_OK'"
            ]
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=cfg.timeout + 5
            )
            if result.returncode == 0 and "SSH_OK" in result.stdout:
                return True, "SSH connection OK"
            else:
                return False, f"SSH failed: {result.stderr}"
        except subprocess.TimeoutExpired:
            return False, "SSH connection timeout"
        except Exception as e:
            return False, f"SSH error: {str(e)}"


class SSHManager:
    """Manage SSH connections and remote operations."""
    
    def __init__(self, ssh_cfg: SSHConfig, nfs_cfg: NFSConfig):
        self.ssh_cfg = ssh_cfg
        self.nfs_cfg = nfs_cfg
    
    def _build_ssh_cmd(self, remote_cmd: str, **kwargs) -> List[str]:
        """Build SSH command."""
        cmd = [
            "ssh",
            "-o", f"ConnectTimeout={self.ssh_cfg.timeout}",
            "-o", "StrictHostKeyChecking=no",
        ]
        
        if self.ssh_cfg.key_file:
            cmd.extend(["-i", self.ssh_cfg.key_file])
        
        cmd.append(f"{self.ssh_cfg.user}@{self.ssh_cfg.host}")
        cmd.append(remote_cmd)
        
        return cmd
    
    def execute_remote(self, cmd: str, timeout: int = 30) -> Tuple[int, str, str]:
        """Execute command on remote board."""
        ssh_cmd = self._build_ssh_cmd(cmd)
        try:
            result = subprocess.run(
                ssh_cmd,
                capture_output=True,
                text=True,
                timeout=timeout
            )
            return result.returncode, result.stdout, result.stderr
        except subprocess.TimeoutExpired:
            return -1, "", "Command timeout"
        except Exception as e:
            return -1, "", str(e)
    
    def check_nfs_mount(self) -> Tuple[bool, str]:
        """Check NFS mount status on remote board."""
        code, stdout, stderr = self.execute_remote("mount | grep nfs")
        if code == 0 and self.nfs_cfg.mount_point in stdout:
            return True, stdout
        return False, stderr
    
    def mount_nfs(self) -> Tuple[bool, str]:
        """Mount NFS on remote board."""
        # Try using the alias first
        code, stdout, stderr = self.execute_remote(self.nfs_cfg.mount_cmd_alias)
        if code == 0:
            return True, f"NFS mounted using alias: {stdout}"
        
        # Fallback to manual mount command
        mount_cmd = f"mount -t nfs {self.nfs_cfg.server_ip}:{self.nfs_cfg.nfs_path} {self.nfs_cfg.mount_point}"
        code, stdout, stderr = self.execute_remote(mount_cmd)
        if code == 0:
            return True, f"NFS mounted manually: {stdout}"
        return False, f"Mount failed: {stderr}"
    
    def unmount_nfs(self) -> Tuple[bool, str]:
        """Unmount NFS on remote board."""
        code, stdout, stderr = self.execute_remote(f"umount {self.nfs_cfg.mount_point}")
        if code == 0:
            return True, "NFS unmounted"
        return False, f"Unmount failed: {stderr}"
    
    def list_remote_dir(self, path: str) -> Tuple[bool, List[str]]:
        """List directory on remote board."""
        code, stdout, stderr = self.execute_remote(f"ls -la {path}")
        if code == 0:
            lines = stdout.strip().split("\n")
            return True, lines
        return False, []
    
    def get_board_info(self) -> Dict[str, str]:
        """Get board system information."""
        info = {}
        
        # Get uname
        code, stdout, _ = self.execute_remote("uname -a")
        if code == 0:
            info["uname"] = stdout.strip()
        
        # Get IP info
        code, stdout, _ = self.execute_remote("ip addr show eth0")
        if code == 0:
            match = re.search(r"inet (\d+\.\d+\.\d+\.\d+)", stdout)
            if match:
                info["board_ip"] = match.group(1)
        
        # Get mount info
        mounted, mount_info = self.check_nfs_mount()
        info["nfs_mounted"] = str(mounted)
        if mounted:
            info["nfs_info"] = mount_info
        
        return info


def cmd_detect(args):
    """Detect network and SSH status."""
    print("=" * 60)
    print("  SSH + NFS Development Environment Detection")
    print("=" * 60)
    
    # 1. Detect WSL IP
    print("\n1. Detecting WSL2/Host IP...")
    wsl_ip = NetworkDetector.get_wsl_ip()
    if wsl_ip:
        print(f"   ✅ WSL2 IP: {wsl_ip}")
    else:
        print("   ❌ Failed to detect WSL2 IP")
        return 1
    
    # 2. Check board connectivity
    print(f"\n2. Checking board connectivity ({args.host})...")
    if NetworkDetector.ping_host(args.host, timeout=2):
        print(f"   ✅ Board {args.host} is reachable")
    else:
        print(f"   ❌ Board {args.host} is NOT reachable")
        return 1
    
    # 3. Check SSH connection
    print(f"\n3. Checking SSH connection to {args.user}@{args.host}...")
    ssh_cfg = SSHConfig(host=args.host, user=args.user, timeout=args.timeout)
    ok, msg = NetworkDetector.check_ssh_connection(ssh_cfg)
    if ok:
        print(f"   ✅ SSH connection OK")
    else:
        print(f"   ❌ SSH connection failed: {msg}")
        return 1
    
    # 4. Check NFS mount
    print(f"\n4. Checking NFS mount on board...")
    nfs_cfg = NFSConfig(server_ip=wsl_ip)
    manager = SSHManager(ssh_cfg, nfs_cfg)
    mounted, mount_info = manager.check_nfs_mount()
    if mounted:
        print(f"   ✅ NFS is mounted")
        print(f"   {mount_info}")
    else:
        print(f"   ⚠️  NFS is NOT mounted")
        print(f"   Run: {args.user}@{args.host}$ mount_nfs_wired")
    
    # 5. Get board info
    print(f"\n5. Getting board information...")
    info = manager.get_board_info()
    for key, value in info.items():
        print(f"   {key}: {value}")
    
    print("\n" + "=" * 60)
    print("  Detection Complete")
    print("=" * 60)
    return 0


def cmd_mount(args):
    """Mount NFS on remote board."""
    ssh_cfg = SSHConfig(host=args.host, user=args.user)
    wsl_ip = NetworkDetector.get_wsl_ip() or args.server_ip
    nfs_cfg = NFSConfig(server_ip=wsl_ip)
    
    manager = SSHManager(ssh_cfg, nfs_cfg)
    
    print(f"Mounting NFS on {args.user}@{args.host}...")
    ok, msg = manager.mount_nfs()
    
    if ok:
        print(f"✅ Success: {msg}")
        return 0
    else:
        print(f"❌ Failed: {msg}")
        return 1


def cmd_unmount(args):
    """Unmount NFS on remote board."""
    ssh_cfg = SSHConfig(host=args.host, user=args.user)
    nfs_cfg = NFSConfig()
    
    manager = SSHManager(ssh_cfg, nfs_cfg)
    
    print(f"Unmounting NFS on {args.user}@{args.host}...")
    ok, msg = manager.unmount_nfs()
    
    if ok:
        print(f"✅ Success: {msg}")
        return 0
    else:
        print(f"❌ Failed: {msg}")
        return 1


def cmd_exec(args):
    """Execute command on remote board."""
    ssh_cfg = SSHConfig(host=args.host, user=args.user)
    manager = SSHManager(ssh_cfg, NFSConfig())
    
    print(f"Executing on {args.user}@{args.host}: {args.cmd}")
    code, stdout, stderr = manager.execute_remote(args.cmd, timeout=args.timeout)
    
    print("\n--- Output ---")
    if stdout:
        print(stdout)
    if stderr:
        print("STDERR:", stderr)
    
    print(f"\nExit code: {code}")
    return code


def cmd_list(args):
    """List directory on remote board."""
    ssh_cfg = SSHConfig(host=args.host, user=args.user)
    manager = SSHManager(ssh_cfg, NFSConfig())
    
    ok, lines = manager.list_remote_dir(args.path)
    
    if ok:
        print(f"Directory: {args.path}")
        for line in lines:
            print(line)
        return 0
    else:
        print(f"Failed to list directory: {args.path}")
        return 1


def build_parser():
    """Build argument parser."""
    parser = argparse.ArgumentParser(description="SSH + NFS Development Manager")
    
    # Global options
    parser.add_argument("--host", default="192.168.5.11", help="Board IP address")
    parser.add_argument("--user", default="root", help="SSH username")
    parser.add_argument("--timeout", type=int, default=10, help="SSH timeout")
    parser.add_argument("--server-ip", default="", help="NFS server IP (auto-detect if empty)")
    
    sub = parser.add_subparsers(dest="subcmd", required=True)
    
    # Detect command
    sub.add_parser("detect", help="Detect network and SSH status")
    
    # Mount command
    sub.add_parser("mount", help="Mount NFS on remote board")
    
    # Unmount command
    sub.add_parser("unmount", help="Unmount NFS on remote board")
    
    # Execute command
    exec_parser = sub.add_parser("exec", help="Execute command on remote board")
    exec_parser.add_argument("cmd", help="Command to execute")
    exec_parser.add_argument("--timeout", type=int, default=30, help="Command timeout")
    
    # List command
    list_parser = sub.add_parser("list", help="List directory on remote board")
    list_parser.add_argument("path", default="/mnt/nfs", help="Directory path to list")
    
    return parser


def main():
    parser = build_parser()
    args = parser.parse_args()
    
    if args.subcmd == "detect":
        return cmd_detect(args)
    elif args.subcmd == "mount":
        return cmd_mount(args)
    elif args.subcmd == "unmount":
        return cmd_unmount(args)
    elif args.subcmd == "exec":
        return cmd_exec(args)
    elif args.subcmd == "list":
        return cmd_list(args)
    else:
        parser.error(f"Unknown command: {args.subcmd}")
        return 2


if __name__ == "__main__":
    sys.exit(main())