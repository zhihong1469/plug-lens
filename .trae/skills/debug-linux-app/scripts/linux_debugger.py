#!/usr/bin/env python3
"""Linux 应用调试工具。

支持：
- 本地 GDB 调试
- 远程 GDB 调试（通过 gdbserver）
- ARM64 交叉调试
- 断点管理
- 调试会话配置
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

if sys.stdout and hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
if sys.stderr and hasattr(sys.stderr, "reconfigure"):
    try:
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

_SCRIPT_DIR = Path(__file__).resolve().parent
_SKILLS_DIR = _SCRIPT_DIR.parent.parent
for _candidate in [_SKILLS_DIR / "shared", _SKILLS_DIR.parent / "shared"]:
    if (_candidate / "tool_config.py").exists():
        sys.path.insert(0, str(_candidate))
        break
from tool_config import get_tool_path


@dataclass
class ToolInfo:
    name: str
    path: str | None
    version: str | None


@dataclass
class DebugResult:
    status: str
    summary: str
    debugger: str | None = None
    debugger_path: str | None = None
    executable: str | None = None
    debug_mode: str | None = None
    target_ip: str | None = None
    target_port: int | None = None
    breakpoints: list[str] = field(default_factory=list)
    commands: list[str] = field(default_factory=list)
    failure_category: str | None = None
    evidence: list[str] = field(default_factory=list)


def find_tool(name: str, alt_names: list[str] | None = None) -> ToolInfo:
    configured = get_tool_path(name)
    if configured:
        configured_path = shutil.which(configured) or configured
        if Path(configured_path).exists():
            version = _get_version(configured_path)
            return ToolInfo(name=name, path=configured_path, version=version)

    candidates = [name] + (alt_names or [])
    for candidate in candidates:
        path = shutil.which(candidate)
        if path:
            version = _get_version(path)
            return ToolInfo(name=candidate, path=path, version=version)
    return ToolInfo(name=name, path=None, version=None)


def _get_version(executable: str) -> str | None:
    try:
        result = subprocess.run(
            [executable, "--version"],
            capture_output=True, text=True, timeout=5,
        )
        first_line = (result.stdout or result.stderr).strip().split("\n")[0]
        return first_line if first_line else None
    except Exception:
        return None


def detect_environment() -> dict[str, Any]:
    gdb = find_tool("gdb")
    gdb_multiarch = find_tool("gdb-multiarch")
    aarch64_gdb = find_tool("aarch64-linux-gnu-gdb")
    
    return {
        "gdb": {"available": gdb.path is not None, "path": gdb.path, "version": gdb.version},
        "gdb_multiarch": {"available": gdb_multiarch.path is not None, "path": gdb_multiarch.path, "version": gdb_multiarch.version},
        "aarch64_gdb": {"available": aarch64_gdb.path is not None, "path": aarch64_gdb.path, "version": aarch64_gdb.version},
    }


def check_executable(executable_path: Path) -> dict[str, Any]:
    result = {
        "exists": executable_path.exists(),
        "has_debug_info": False,
        "architecture": None,
    }
    
    if not result["exists"]:
        return result
    
    try:
        result_output = subprocess.run(
            ["file", str(executable_path)],
            capture_output=True, text=True, timeout=10,
        )
        file_info = result_output.stdout
        result["architecture"] = "arm64" if "aarch64" in file_info.lower() else "x86_64"
        
        readelf_output = subprocess.run(
            ["readelf", "-S", str(executable_path)],
            capture_output=True, text=True, timeout=10,
        )
        result["has_debug_info"] = ".debug_info" in readelf_output.stdout
    except Exception:
        pass
    
    return result


def generate_gdb_commands(
    executable: str,
    debug_mode: str,
    target_ip: str | None = None,
    target_port: int | None = None,
    breakpoints: list[str] = None,
) -> list[str]:
    commands = []
    commands.append(f"file {executable}")
    
    if debug_mode == "remote" and target_ip and target_port:
        commands.append(f"target remote {target_ip}:{target_port}")
    
    if breakpoints:
        for bp in breakpoints:
            commands.append(f"break {bp}")
    
    if debug_mode == "local":
        commands.append("run")
    
    return commands


def generate_gdb_script(commands: list[str], script_path: Path) -> None:
    script_content = "\n".join(commands) + "\n"
    script_path.write_text(script_content, encoding="utf-8")


def select_debugger(arch: str) -> ToolInfo:
    if arch == "arm64":
        aarch64_gdb = find_tool("aarch64-linux-gnu-gdb")
        if aarch64_gdb.path:
            return aarch64_gdb
        gdb_multiarch = find_tool("gdb-multiarch")
        if gdb_multiarch.path:
            return gdb_multiarch
    return find_tool("gdb")


def result_to_json(result: DebugResult) -> str:
    data = {
        "status": result.status,
        "summary": result.summary,
        "debugger": result.debugger,
        "debugger_path": result.debugger_path,
        "executable": result.executable,
        "debug_mode": result.debug_mode,
        "target_ip": result.target_ip,
        "target_port": result.target_port,
        "breakpoints": result.breakpoints,
        "commands": result.commands,
        "failure_category": result.failure_category,
        "evidence": result.evidence,
    }
    return json.dumps(data, indent=2, ensure_ascii=False)


def main():
    parser = argparse.ArgumentParser(description="Linux 应用调试工具")
    parser.add_argument("--executable", type=str, required=False, help="目标可执行文件路径")
    parser.add_argument("--local", action="store_true", help="本地调试模式")
    parser.add_argument("--remote", action="store_true", help="远程调试模式")
    parser.add_argument("--target-ip", type=str, default="127.0.0.1", help="远程目标 IP 地址")
    parser.add_argument("--target-port", type=int, default=1234, help="远程目标端口")
    parser.add_argument("--break", type=str, action="append", dest="breakpoints", default=[], help="断点位置")
    parser.add_argument("--detect", action="store_true", help="探测调试环境")
    parser.add_argument("--dry-run", action="store_true", help="仅生成调试命令，不执行")
    parser.add_argument("--json", action="store_true", help="输出 JSON 格式结果")
    parser.add_argument("--arch", type=str, choices=["x86_64", "arm64"], help="目标架构")
    
    args = parser.parse_args()
    
    if args.detect:
        env = detect_environment()
        print(json.dumps(env, indent=2, ensure_ascii=False))
        return
    
    if not args.executable:
        result = DebugResult(
            status="failure",
            summary="缺少可执行文件参数",
            failure_category="project-config-error",
            evidence=["必须指定 --executable 参数"],
        )
        print(result_to_json(result) if args.json else f"❌ {result.summary}")
        return
    
    executable_path = Path(args.executable)
    exec_info = check_executable(executable_path)
    
    if not exec_info["exists"]:
        result = DebugResult(
            status="failure",
            summary=f"可执行文件不存在: {executable_path}",
            failure_category="project-config-error",
            evidence=[f"文件 {executable_path} 不存在"],
        )
        print(result_to_json(result) if args.json else f"❌ {result.summary}")
        return
    
    if not exec_info["has_debug_info"]:
        print("⚠️ 警告: 可执行文件可能缺少调试符号")
    
    arch = args.arch or exec_info.get("architecture") or "x86_64"
    debugger = select_debugger(arch)
    
    if not debugger.path:
        result = DebugResult(
            status="failure",
            summary="未找到合适的 GDB 调试器",
            failure_category="environment-missing",
            evidence=["无法找到 gdb、gdb-multiarch 或 aarch64-linux-gnu-gdb"],
        )
        print(result_to_json(result) if args.json else f"❌ {result.summary}")
        return
    
    debug_mode = "remote" if args.remote else "local"
    
    commands = generate_gdb_commands(
        executable=str(executable_path),
        debug_mode=debug_mode,
        target_ip=args.target_ip if args.remote else None,
        target_port=args.target_port if args.remote else None,
        breakpoints=args.breakpoints,
    )
    
    if args.dry_run:
        result = DebugResult(
            status="success",
            summary="调试命令已生成（dry-run 模式）",
            debugger=debugger.name,
            debugger_path=debugger.path,
            executable=str(executable_path),
            debug_mode=debug_mode,
            target_ip=args.target_ip if args.remote else None,
            target_port=args.target_port if args.remote else None,
            breakpoints=args.breakpoints,
            commands=commands,
        )
        print(result_to_json(result) if args.json else f"\n📋 调试命令:\n" + "\n".join(commands))
        return
    
    gdb_script = Path("/tmp/gdb_script.gdb")
    generate_gdb_script(commands, gdb_script)
    
    print(f"🔧 使用调试器: {debugger.path}")
    print(f"🎯 目标文件: {executable_path}")
    print(f"🔌 调试模式: {debug_mode}")
    if debug_mode == "remote":
        print(f"🌐 远程目标: {args.target_ip}:{args.target_port}")
    
    result = DebugResult(
        status="success",
        summary="调试会话已准备就绪",
        debugger=debugger.name,
        debugger_path=debugger.path,
        executable=str(executable_path),
        debug_mode=debug_mode,
        target_ip=args.target_ip if args.remote else None,
        target_port=args.target_port if args.remote else None,
        breakpoints=args.breakpoints,
        commands=commands,
    )
    
    if args.json:
        print(result_to_json(result))
    else:
        print("\n📊 调试配置完成")
        print(f"💡 运行以下命令启动调试:\n{debugger.path} -x {gdb_script}")


if __name__ == "__main__":
    main()