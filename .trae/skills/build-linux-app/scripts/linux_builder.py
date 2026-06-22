#!/usr/bin/env python3
"""Linux 应用构建工具。

支持：
- x86_64 本地编译和 ARM64 交叉编译
- CMake 和 Makefile 构建系统
- 自动工具链探测
- CMakePresets.json 支持
- 产物扫描和结果输出
"""

from __future__ import annotations

import argparse
import json
import os
import re
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
from tool_config import get_tool_path, set_tool_path


ARTIFACT_EXTENSIONS = {".elf": "elf", ".bin": "bin", ".so": "shared_lib", ".a": "static_lib", ".out": "executable"}
GENERATOR_PRIORITY = ["Ninja", "Unix Makefiles", "MinGW Makefiles"]


@dataclass
class ToolInfo:
    name: str
    path: str | None
    version: str | None


@dataclass
class Preset:
    name: str
    display_name: str
    description: str
    generator: str | None
    build_type: str | None
    toolchain: str | None


@dataclass
class Artifact:
    path: Path
    kind: str
    size: int


@dataclass
class BuildResult:
    status: str  # success, failure, blocked
    summary: str
    configure_cmd: str | None = None
    build_cmd: str | None = None
    build_dir: str | None = None
    generator: str | None = None
    artifacts: list[Artifact] = field(default_factory=list)
    primary_artifact: Artifact | None = None
    failure_category: str | None = None
    evidence: list[str] = field(default_factory=list)


def find_tool(name: str, alt_names: list[str] | None = None) -> ToolInfo:
    configured = get_tool_path(name)
    alt_names = alt_names or []
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


def detect_environment(arch: str | None = None) -> dict[str, Any]:
    gcc = find_tool("gcc", ["gcc.exe"])
    gpp = find_tool("g++", ["g++.exe"])
    cmake = find_tool("cmake", ["cmake.exe"])
    ninja = find_tool("ninja", ["ninja.exe"])
    make = find_tool("make", ["gmake", "mingw32-make"])
    
    cross_gcc = None
    cross_gpp = None
    if arch == "arm64":
        # 优先读取环境变量$CROSS_COMPILE，就是你use_toolchain设置的变量
        cross_prefix = os.environ.get("CROSS_COMPILE", "")
        if cross_prefix:
            cross_gcc = find_tool(f"{cross_prefix}gcc")
            cross_gpp = find_tool(f"{cross_prefix}g++")
        else:
            # 支持所有RK常用的交叉编译器前缀，按优先级排序
            cross_gcc = find_tool("aarch64-none-linux-gnu-gcc", ["aarch64-linux-gnu-gcc", "aarch64-linux-musl-gcc"])
            cross_gpp = find_tool("aarch64-none-linux-gnu-g++", ["aarch64-linux-gnu-g++", "aarch64-linux-musl-g++"])

    env = {
        "host_compiler": {
            "gcc": {"available": gcc.path is not None, "path": gcc.path, "version": gcc.version},
            "g++": {"available": gpp.path is not None, "path": gpp.path, "version": gpp.version},
        },
        "cmake": {"available": cmake.path is not None, "path": cmake.path, "version": cmake.version},
        "ninja": {"available": ninja.path is not None, "path": ninja.path, "version": ninja.version},
        "make": {"available": make.path is not None, "path": make.path, "version": make.version},
    }
    
    if arch == "arm64":
        env["cross_compiler"] = {
            "aarch64-linux-gnu-gcc": {"available": cross_gcc.path is not None, "path": cross_gcc.path, "version": cross_gcc.version},
            "aarch64-linux-gnu-g++": {"available": cross_gpp.path is not None, "path": cross_gpp.path, "version": cross_gpp.version},
        }
    
    return env


def load_presets(source_dir: Path) -> list[Preset]:
    presets_file = source_dir / "CMakePresets.json"
    if not presets_file.exists():
        return []

    try:
        data = json.loads(presets_file.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as exc:
        print(f"⚠️ 无法解析 CMakePresets.json: {exc}")
        return []

    configure_presets = data.get("configurePresets", [])
    results: list[Preset] = []
    for p in configure_presets:
        if p.get("hidden", False):
            continue
        cache_vars = p.get("cacheVariables", {})
        results.append(Preset(
            name=p.get("name", ""),
            display_name=p.get("displayName", p.get("name", "")),
            description=p.get("description", ""),
            generator=p.get("generator"),
            build_type=cache_vars.get("CMAKE_BUILD_TYPE"),
            toolchain=p.get("toolchainFile") or cache_vars.get("CMAKE_TOOLCHAIN_FILE"),
        ))
    return results


def list_presets_display(source_dir: Path) -> list[Preset]:
    presets = load_presets(source_dir)
    if not presets:
        print("❌ 未找到可用的 CMake 预设")
        presets_file = source_dir / "CMakePresets.json"
        if not presets_file.exists():
            print(f"   {presets_file} 不存在")
        return []

    print("📋 可用 CMake 预设：")
    for i, p in enumerate(presets, 1):
        gen_info = f" [{p.generator}]" if p.generator else ""
        bt_info = f" ({p.build_type})" if p.build_type else ""
        desc = f" - {p.description}" if p.description else ""
        print(f"  {i}. {p.name}{gen_info}{bt_info}{desc}")
    return presets


def scan_cmakelists(source_dir: Path) -> dict[str, str | None]:
    cmakelists = source_dir / "CMakeLists.txt"
    info: dict[str, str | None] = {"project_name": None, "toolchain_hint": None}
    if not cmakelists.exists():
        return info

    try:
        content = cmakelists.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return info

    project_match = re.search(r"project\s*\(\s*(\w+)", content, re.IGNORECASE)
    if project_match:
        info["project_name"] = project_match.group(1)

    tc_match = re.search(r"CMAKE_TOOLCHAIN_FILE\s+[\"']?([^\s\"')]+)", content)
    if tc_match:
        info["toolchain_hint"] = tc_match.group(1)

    return info


def scan_artifacts(build_dir: Path) -> list[Artifact]:
    if not build_dir.exists():
        return []

    artifacts: list[Artifact] = []
    seen: set[str] = set()
    for root, _dirs, files in os.walk(build_dir):
        for fname in files:
            ext = Path(fname).suffix.lower()
            kind = ARTIFACT_EXTENSIONS.get(ext)
            if not kind:
                if not ext:
                    kind = "executable"
                else:
                    continue
            fpath = Path(root) / fname
            real = str(fpath.resolve())
            if real in seen:
                continue
            seen.add(real)
            try:
                size = fpath.stat().st_size
            except OSError:
                size = 0
            if size < 64:
                continue
            artifacts.append(Artifact(path=fpath, kind=kind, size=size))

    return artifacts


def pick_primary_artifact(artifacts: list[Artifact]) -> Artifact | None:
    if not artifacts:
        return None
    exe_artifacts = [a for a in artifacts if a.kind == "executable" or a.kind == "elf"]
    if exe_artifacts:
        return max(exe_artifacts, key=lambda x: x.size)
    return artifacts[0]


def run_cmake_configure(
    source_dir: Path,
    build_dir: Path,
    preset: str | None,
    generator: str | None,
    build_type: str | None,
    toolchain: str | None,
    arch: str | None,
    extra_args: list[str],
) -> tuple[bool, str, list[str]]:
    cmd: list[str] = ["cmake"]

    if preset:
        cmd.extend(["--preset", preset])
        if source_dir:
            cmd.extend(["-S", str(source_dir)])
    else:
        cmd.extend(["-S", str(source_dir), "-B", str(build_dir)])
        if generator:
            cmd.extend(["-G", generator])
        if build_type:
            cmd.append(f"-DCMAKE_BUILD_TYPE={build_type}")
        if toolchain:
            cmd.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain}")
        if arch == "arm64":
            # 优先用环境变量的前缀，没有的话用探测到的工具链前缀
            cross_prefix = os.environ.get("CROSS_COMPILE", None)
            if not cross_prefix:
                cross_gcc_path = get_tool_path("aarch64-none-linux-gnu-gcc") or get_tool_path("aarch64-linux-gnu-gcc")
                if cross_gcc_path:
                    cross_prefix = cross_gcc_path.replace("gcc", "")
                else:
                    cross_prefix = "aarch64-none-linux-gnu-" # RK默认前缀
            cmd.append(f"-DCMAKE_C_COMPILER={cross_prefix}gcc")
            cmd.append(f"-DCMAKE_CXX_COMPILER={cross_prefix}g++")
            cmd.append(f"-DCMAKE_SYSTEM_NAME=Linux")
            cmd.append(f"-DCMAKE_SYSTEM_PROCESSOR=aarch64")

    cmd.extend(extra_args)
    cmd_str = " ".join(cmd)
    print(f"🔧 配置命令: {cmd_str}")

    evidence = []
    try:
        result = subprocess.run(
            cmd,
            cwd=source_dir if source_dir else None,
            capture_output=True,
            text=True,
            timeout=300,
        )
        evidence.append(f"配置输出:\n{result.stdout}")
        if result.stderr:
            evidence.append(f"配置错误:\n{result.stderr}")
        
        if result.returncode != 0:
            return False, cmd_str, evidence
        return True, cmd_str, evidence
    except subprocess.TimeoutExpired:
        return False, cmd_str, ["配置超时"]
    except Exception as e:
        return False, cmd_str, [f"配置异常: {e}"]


def run_cmake_build(build_dir: Path, target: str | None, parallel: int = 0) -> tuple[bool, str, list[str]]:
    cmd = ["cmake", "--build", str(build_dir)]
    if target:
        cmd.extend(["--target", target])
    if parallel > 0:
        cmd.extend(["-j", str(parallel)])
    
    cmd_str = " ".join(cmd)
    print(f"🔨 构建命令: {cmd_str}")

    evidence = []
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=600,
        )
        evidence.append(f"构建输出:\n{result.stdout}")
        if result.stderr:
            evidence.append(f"构建错误:\n{result.stderr}")
        
        if result.returncode != 0:
            return False, cmd_str, evidence
        return True, cmd_str, evidence
    except subprocess.TimeoutExpired:
        return False, cmd_str, ["构建超时"]
    except Exception as e:
        return False, cmd_str, [f"构建异常: {e}"]


def run_make(build_dir: Path, target: str | None, parallel: int = 0, env: dict | None = None) -> tuple[bool, str, list[str]]:
    cmd = ["make"]
    if parallel > 0:
        cmd.append(f"-j{parallel}")
    if target:
        cmd.append(target)
    
    cmd_str = " ".join(cmd)
    
    # 添加环境变量信息到命令显示
    env_info = ""
    if env:
        env_vars = []
        if "CROSS_COMPILE" in env:
            env_vars.append(f"CROSS_COMPILE={env['CROSS_COMPILE']}")
        if "PLATFORM" in env:
            env_vars.append(f"PLATFORM={env['PLATFORM']}")
        if env_vars:
            env_info = " ".join(env_vars) + " "
    
    print(f"🔨 构建命令: {env_info}{cmd_str}")

    evidence = []
    try:
        result = subprocess.run(
            cmd,
            cwd=build_dir,
            capture_output=True,
            text=True,
            timeout=600,
            env=env,
        )
        evidence.append(f"构建输出:\n{result.stdout}")
        if result.stderr:
            evidence.append(f"构建错误:\n{result.stderr}")
        
        if result.returncode != 0:
            return False, cmd_str, evidence
        return True, cmd_str, evidence
    except subprocess.TimeoutExpired:
        return False, cmd_str, ["构建超时"]
    except Exception as e:
        return False, cmd_str, [f"构建异常: {e}"]


def build_cmake(
    source_dir: Path,
    build_dir: Path,
    preset: str | None,
    generator: str | None,
    build_type: str | None,
    toolchain: str | None,
    arch: str | None,
    target: str | None,
    parallel: int,
    extra_args: list[str],
) -> BuildResult:
    configure_ok, configure_cmd, configure_evidence = run_cmake_configure(
        source_dir, build_dir, preset, generator, build_type, toolchain, arch, extra_args
    )
    
    if not configure_ok:
        return BuildResult(
            status="failure",
            summary="CMake 配置失败",
            configure_cmd=configure_cmd,
            failure_category="project-config-error",
            evidence=configure_evidence,
        )
    
    build_ok, build_cmd, build_evidence = run_cmake_build(build_dir, target, parallel)
    
    if not build_ok:
        return BuildResult(
            status="failure",
            summary="CMake 构建失败",
            configure_cmd=configure_cmd,
            build_cmd=build_cmd,
            build_dir=str(build_dir),
            failure_category="project-config-error",
            evidence=configure_evidence + build_evidence,
        )
    
    artifacts = scan_artifacts(build_dir)
    primary_artifact = pick_primary_artifact(artifacts)
    
    if not artifacts:
        return BuildResult(
            status="failure",
            summary="构建成功但未找到产物",
            configure_cmd=configure_cmd,
            build_cmd=build_cmd,
            build_dir=str(build_dir),
            failure_category="artifact-missing",
            evidence=configure_evidence + build_evidence,
        )
    
    return BuildResult(
        status="success",
        summary="构建成功",
        configure_cmd=configure_cmd,
        build_cmd=build_cmd,
        build_dir=str(build_dir),
        generator=generator,
        artifacts=artifacts,
        primary_artifact=primary_artifact,
        evidence=configure_evidence + build_evidence,
    )


def build_makefile(
    source_dir: Path,
    target: str | None,
    parallel: int,
    arch: str | None = None,
) -> BuildResult:
    # 设置平台和交叉编译环境变量
    env = os.environ.copy()
    
    if arch == "arm64":
        # 优先使用环境变量中的交叉编译器前缀
        cross_prefix = os.environ.get("CROSS_COMPILE", "")
        if not cross_prefix:
            # 默认使用 ARM64 交叉编译器
            cross_prefix = "aarch64-linux-gnu-"
        env["CROSS_COMPILE"] = cross_prefix
        env["PLATFORM"] = "rk3562"
    else:
        # 默认使用 i.MX6ULL 平台配置
        env["PLATFORM"] = os.environ.get("PLATFORM", "imx6ull")
    
    build_ok, build_cmd, evidence = run_make(source_dir, target, parallel, env)
    
    if not build_ok:
        return BuildResult(
            status="failure",
            summary="Makefile 构建失败",
            build_cmd=build_cmd,
            build_dir=str(source_dir),
            failure_category="project-config-error",
            evidence=evidence,
        )
    
    artifacts = scan_artifacts(source_dir)
    primary_artifact = pick_primary_artifact(artifacts)
    
    if not artifacts:
        return BuildResult(
            status="failure",
            summary="构建成功但未找到产物",
            build_cmd=build_cmd,
            build_dir=str(source_dir),
            failure_category="artifact-missing",
            evidence=evidence,
        )
    
    return BuildResult(
        status="success",
        summary="构建成功",
        build_cmd=build_cmd,
        build_dir=str(source_dir),
        artifacts=artifacts,
        primary_artifact=primary_artifact,
        evidence=evidence,
    )


def result_to_json(result: BuildResult) -> str:
    data = {
        "status": result.status,
        "summary": result.summary,
        "configure_cmd": result.configure_cmd,
        "build_cmd": result.build_cmd,
        "build_dir": result.build_dir,
        "generator": result.generator,
        "failure_category": result.failure_category,
        "evidence": result.evidence,
        "artifacts": [
            {"path": str(a.path), "kind": a.kind, "size": a.size}
            for a in result.artifacts
        ],
        "primary_artifact": str(result.primary_artifact.path) if result.primary_artifact else None,
    }
    return json.dumps(data, indent=2, ensure_ascii=False)


def main():
    parser = argparse.ArgumentParser(description="Linux 应用构建工具")
    parser.add_argument("--source", type=str, help="源目录路径")
    parser.add_argument("--build-dir", type=str, help="构建目录路径")
    parser.add_argument("--preset", type=str, help="CMake 预设名称")
    parser.add_argument("--generator", type=str, help="CMake 生成器")
    parser.add_argument("--build-type", type=str, default="Release", help="构建类型 (Debug/Release/RelWithDebInfo)")
    parser.add_argument("--toolchain", type=str, help="工具链文件路径")
    parser.add_argument("--arch", type=str, choices=["x86_64", "arm64"], help="目标架构")
    parser.add_argument("--target", type=str, help="构建目标")
    parser.add_argument("--parallel", type=int, default=0, help="并行构建线程数")
    parser.add_argument("--build-system", type=str, choices=["cmake", "makefile"], default="cmake", help="构建系统")
    parser.add_argument("--detect", action="store_true", help="探测构建环境")
    parser.add_argument("--list-presets", action="store_true", help="列出可用 CMake 预设")
    parser.add_argument("--extra-args", type=str, nargs="*", default=[], help="额外的 CMake 参数")
    parser.add_argument("--json", action="store_true", help="输出 JSON 格式结果")
    
    args = parser.parse_args()
    
    if args.detect:
        env = detect_environment(args.arch)
        print(json.dumps(env, indent=2, ensure_ascii=False))
        return
    
    if not args.source:
        print("❌ 错误: 必须指定 --source 参数")
        sys.exit(1)
    
    source_dir = Path(args.source)
    if not source_dir.exists():
        print(f"❌ 错误: 源目录不存在: {source_dir}")
        sys.exit(1)
    
    if args.list_presets:
        list_presets_display(source_dir)
        return
    
    build_dir = Path(args.build_dir) if args.build_dir else source_dir / "build"
    
    if args.build_system == "cmake":
        result = build_cmake(
            source_dir=source_dir,
            build_dir=build_dir,
            preset=args.preset,
            generator=args.generator,
            build_type=args.build_type,
            toolchain=args.toolchain,
            arch=args.arch,
            target=args.target,
            parallel=args.parallel,
            extra_args=args.extra_args,
        )
    else:
        result = build_makefile(
            source_dir=source_dir,
            target=args.target,
            parallel=args.parallel,
            arch=args.arch,
        )
    
    if args.json:
        print(result_to_json(result))
    else:
        print(f"\n📊 构建结果: {result.status}")
        print(f"📝 摘要: {result.summary}")
        if result.build_dir:
            print(f"📁 构建目录: {result.build_dir}")
        if result.primary_artifact:
            print(f"🎯 主产物: {result.primary_artifact.path} ({result.primary_artifact.size} bytes)")
        if result.failure_category:
            print(f"❌ 失败分类: {result.failure_category}")


if __name__ == "__main__":
    main()