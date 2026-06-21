#!/usr/bin/env python3
"""File sync and remote execution for NFS-based development workflow."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple


@dataclass
class SyncConfig:
    """File sync configuration."""
    local_path: str  # Local build output directory
    remote_path: str = "/mnt/nfs"  # NFS mount point on board
    board_host: str = "192.168.5.11"
    board_user: str = "root"


class NFSFileSync:
    """Sync files through NFS mount."""
    
    def __init__(self, cfg: SyncConfig):
        self.cfg = cfg
    
    def check_nfs_mounted(self) -> bool:
        """Check if NFS is mounted on board."""
        try:
            result = subprocess.run(
                [
                    "ssh",
                    "-o", "ConnectTimeout=5",
                    "-o", "StrictHostKeyChecking=no",
                    f"{self.cfg.board_user}@{self.cfg.board_host}",
                    "mount | grep nfs"
                ],
                capture_output=True,
                text=True,
                timeout=10
            )
            return result.returncode == 0 and self.cfg.remote_path in result.stdout
        except Exception:
            return False
    
    def sync_file(self, local_file: str, remote_subdir: str = "") -> Tuple[bool, str]:
        """
        Sync a single file to board via NFS.
        
        Since NFS is mounted, we can directly copy from local NFS path
        to board's NFS mount point.
        """
        local_path = Path(local_file)
        
        if not local_path.exists():
            return False, f"Local file not found: {local_file}"
        
        # Determine remote target path
        if remote_subdir:
            target_dir = f"{self.cfg.remote_path}/{remote_subdir}"
        else:
            target_dir = self.cfg.remote_path
        
        # Copy file via SSH (more reliable than direct NFS copy)
        try:
            result = subprocess.run(
                [
                    "scp",
                    "-o", "StrictHostKeyChecking=no",
                    local_file,
                    f"{self.cfg.board_user}@{self.cfg.board_host}:{target_dir}/"
                ],
                capture_output=True,
                text=True,
                timeout=30
            )
            
            if result.returncode == 0:
                filename = local_path.name
                return True, f"Synced {filename} to {target_dir}"
            else:
                return False, f"SCP failed: {result.stderr}"
        except Exception as e:
            return False, f"Sync error: {str(e)}"
    
    def sync_directory(self, local_dir: str, remote_subdir: str = "") -> Tuple[bool, str]:
        """Sync entire directory to board."""
        local_path = Path(local_dir)
        
        if not local_path.exists():
            return False, f"Local directory not found: {local_dir}"
        
        # Determine remote target path
        if remote_subdir:
            target_dir = f"{self.cfg.remote_path}/{remote_subdir}"
        else:
            target_dir = self.cfg.remote_path
        
        # Use rsync for efficient sync
        try:
            result = subprocess.run(
                [
                    "rsync",
                    "-avz",
                    "--progress",
                    local_dir,
                    f"{self.cfg.board_user}@{self.cfg.board_host}:{target_dir}/"
                ],
                capture_output=True,
                text=True,
                timeout=60
            )
            
            if result.returncode == 0:
                return True, f"Synced directory to {target_dir}\n{result.stdout}"
            else:
                return False, f"Rsync failed: {result.stderr}"
        except Exception as e:
            return False, f"Sync error: {str(e)}"


class RemoteExecutor:
    """Execute programs on remote board."""
    
    def __init__(self, board_host: str = "192.168.5.11", board_user: str = "root"):
        self.board_host = board_host
        self.board_user = board_user
    
    def run_program(
        self,
        program_path: str,
        args: str = "",
        timeout: int = 30,
        background: bool = False
    ) -> Tuple[int, str, str]:
        """Execute program on remote board."""
        
        # Build command
        if background:
            cmd = f"cd {Path(program_path).parent} && nohup {program_path} {args} > /tmp/prog.log 2>&1 &"
        else:
            cmd = f"cd {Path(program_path).parent} && {program_path} {args}"
        
        # Execute via SSH
        ssh_cmd = [
            "ssh",
            "-o", "ConnectTimeout=10",
            "-o", "StrictHostKeyChecking=no",
            f"{self.board_user}@{self.board_host}",
            cmd
        ]
        
        try:
            result = subprocess.run(
                ssh_cmd,
                capture_output=True,
                text=True,
                timeout=timeout
            )
            return result.returncode, result.stdout, result.stderr
        except subprocess.TimeoutExpired:
            return -1, "", "Execution timeout"
        except Exception as e:
            return -1, "", str(e)
    
    def kill_program(self, program_name: str) -> Tuple[bool, str]:
        """Kill running program on board."""
        ssh_cmd = [
            "ssh",
            "-o", "ConnectTimeout=5",
            "-o", "StrictHostKeyChecking=no",
            f"{self.board_user}@{self.board_host}",
            f"killall {program_name}"
        ]
        
        try:
            result = subprocess.run(
                ssh_cmd,
                capture_output=True,
                text=True,
                timeout=10
            )
            
            if result.returncode == 0:
                return True, f"Killed {program_name}"
            else:
                return False, f"Kill failed: {result.stderr}"
        except Exception as e:
            return False, str(e)
    
    def check_program_running(self, program_name: str) -> Tuple[bool, str]:
        """Check if program is running on board."""
        ssh_cmd = [
            "ssh",
            "-o", "ConnectTimeout=5",
            "-o", "StrictHostKeyChecking=no",
            f"{self.board_user}@{self.board_host}",
            f"ps aux | grep {program_name} | grep -v grep"
        ]
        
        try:
            result = subprocess.run(
                ssh_cmd,
                capture_output=True,
                text=True,
                timeout=10
            )
            
            if result.returncode == 0 and result.stdout.strip():
                return True, result.stdout
            return False, "Not running"
        except Exception as e:
            return False, str(e)


def cmd_sync_file(args):
    """Sync single file to board."""
    cfg = SyncConfig(
        local_path=args.local_file,
        remote_path=args.remote_path,
        board_host=args.host,
        board_user=args.user
    )
    
    syncer = NFSFileSync(cfg)
    
    print(f"Syncing {args.local_file} to {args.host}:{args.remote_path}/{args.subdir}")
    ok, msg = syncer.sync_file(args.local_file, args.subdir)
    
    if ok:
        print(f"✅ {msg}")
        return 0
    else:
        print(f"❌ {msg}")
        return 1


def cmd_sync_dir(args):
    """Sync directory to board."""
    cfg = SyncConfig(
        local_path=args.local_dir,
        remote_path=args.remote_path,
        board_host=args.host,
        board_user=args.user
    )
    
    syncer = NFSFileSync(cfg)
    
    print(f"Syncing {args.local_dir} to {args.host}:{args.remote_path}/{args.subdir}")
    ok, msg = syncer.sync_directory(args.local_dir, args.subdir)
    
    if ok:
        print(f"✅ {msg}")
        return 0
    else:
        print(f"❌ {msg}")
        return 1


def cmd_run(args):
    """Run program on board."""
    executor = RemoteExecutor(board_host=args.host, board_user=args.user)
    
    print(f"Running {args.program} on {args.host}...")
    code, stdout, stderr = executor.run_program(
        args.program,
        args=args.args,
        timeout=args.timeout,
        background=args.background
    )
    
    print("\n--- Output ---")
    if stdout:
        print(stdout)
    if stderr:
        print("STDERR:", stderr)
    
    print(f"\nExit code: {code}")
    
    if args.background:
        print("\nProgram running in background. Check log: /tmp/prog.log")
    
    return code


def cmd_kill(args):
    """Kill program on board."""
    executor = RemoteExecutor(board_host=args.host, board_user=args.user)
    
    print(f"Killing {args.program} on {args.host}...")
    ok, msg = executor.kill_program(args.program)
    
    if ok:
        print(f"✅ {msg}")
        return 0
    else:
        print(f"❌ {msg}")
        return 1


def cmd_check(args):
    """Check if program is running."""
    executor = RemoteExecutor(board_host=args.host, board_user=args.user)
    
    print(f"Checking {args.program} on {args.host}...")
    running, info = executor.check_program_running(args.program)
    
    if running:
        print(f"✅ Program is running")
        print(info)
        return 0
    else:
        print(f"⚠️  {info}")
        return 1


def build_parser():
    """Build argument parser."""
    parser = argparse.ArgumentParser(description="NFS File Sync and Remote Execution")
    
    # Global options
    parser.add_argument("--host", default="192.168.5.11", help="Board IP address")
    parser.add_argument("--user", default="root", help="SSH username")
    parser.add_argument("--remote-path", default="/mnt/nfs", help="Remote NFS mount point")
    
    sub = parser.add_subparsers(dest="subcmd", required=True)
    
    # Sync file command
    sync_file_parser = sub.add_parser("sync-file", help="Sync single file to board")
    sync_file_parser.add_argument("local_file", help="Local file path")
    sync_file_parser.add_argument("--subdir", default="", help="Remote subdirectory")
    
    # Sync directory command
    sync_dir_parser = sub.add_parser("sync-dir", help="Sync directory to board")
    sync_dir_parser.add_argument("local_dir", help="Local directory path")
    sync_dir_parser.add_argument("--subdir", default="", help="Remote subdirectory")
    
    # Run command
    run_parser = sub.add_parser("run", help="Run program on board")
    run_parser.add_argument("program", help="Program path on board")
    run_parser.add_argument("--args", default="", help="Program arguments")
    run_parser.add_argument("--timeout", type=int, default=30, help="Execution timeout")
    run_parser.add_argument("--background", action="store_true", help="Run in background")
    
    # Kill command
    kill_parser = sub.add_parser("kill", help="Kill program on board")
    kill_parser.add_argument("program", help="Program name to kill")
    
    # Check command
    check_parser = sub.add_parser("check", help="Check if program is running")
    check_parser.add_argument("program", help="Program name to check")
    
    return parser


def main():
    parser = build_parser()
    args = parser.parse_args()
    
    if args.subcmd == "sync-file":
        return cmd_sync_file(args)
    elif args.subcmd == "sync-dir":
        return cmd_sync_dir(args)
    elif args.subcmd == "run":
        return cmd_run(args)
    elif args.subcmd == "kill":
        return cmd_kill(args)
    elif args.subcmd == "check":
        return cmd_check(args)
    else:
        parser.error(f"Unknown command: {args.subcmd}")
        return 2


if __name__ == "__main__":
    sys.exit(main())