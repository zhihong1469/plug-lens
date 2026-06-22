---
name: ssh-nfs-dev
description: 用于嵌入式Linux开发板的SSH远程连接和NFS网络文件共享开发流程。支持自动连接、文件同步、远程执行等操作。
---

# SSH + NFS 远程开发技能

## 适用场景

- 需要通过SSH连接到嵌入式Linux开发板（如RK3562）进行远程开发。
- 使用NFS网络文件系统将编译产物实时同步到开发板。
- 在开发板上远程执行程序并查看输出。
- 需要高效的开发-测试循环流程。

## 必要输入

- 开发板IP地址（默认：192.168.5.11）
- SSH用户名（默认：root）
- NFS挂载点（默认：/mnt/nfs）
- 工作区路径或Project Profile

## 自动探测

- 自动检测WSL2/宿主机IP地址。
- 检测SSH连接状态（是否已连接）。
- 检测NFS挂载状态（开发板端）。
- 检测项目构建系统和产物路径。

## 执行步骤

1. 先阅读 [references/usage.md](references/usage.md)，确认本次是连接、挂载、同步还是执行。
2. 运行脚本 [scripts/linux/ssh_manager.py](scripts/linux/ssh_manager.py) 的 `--detect` 模式确认网络和SSH状态。
3. 根据目标执行相应操作：
   - **connect**: 建立SSH连接
   - **mount**: 在开发板上挂载NFS
   - **sync**: 同步编译产物到开发板
   - **run**: 在开发板上执行程序
4. 将连接信息、挂载状态和执行结果写回Project Profile。

## 失败分流

- 当SSH连接失败时，返回 `connection-error`。
- 当NFS挂载失败时，返回 `mount-error`。
- 当文件同步失败时，返回 `sync-error`。
- 当远程执行失败时，返回 `execution-error`。

## 平台说明

- 在WSL2上运行，支持与开发板的以太网直连。
- 开发板端需预先配置NFS服务端和SSH服务。
- IDE已支持SSH远程挂载功能。

## 输出约定

- 输出SSH连接状态和命令执行结果。
- 更新Project Profile中的SSH和NFS配置信息。

## 网络配置示例

```
WSL2 IP:      192.168.5.10
开发板 IP:    192.168.5.11
子网掩码:     255.255.255.0
NFS挂载点:    /mnt/nfs
SSH用户:      root
```

## 开发板预设命令

- `mount_nfs_wired`: 快速挂载NFS（已配置）
- `/mnt/nfs/run_on_board`: 运行目录
- `/mnt/nfs/run_on_board_rk3562`: RK3562专用运行目录