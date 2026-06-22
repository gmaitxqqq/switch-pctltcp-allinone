# Switch 家长控制管理器

## 🎯 项目系列演进故事

本项目历经 **6 次迭代**，逐步从本机工具演进为完善的双通道远程管理方案：

| 版本 | 仓库 | 日期 | 核心改进 |
|------|------|------|----------|
| V1 | switch-parental-timer | 05-25 | 本机 NRO 工具，直接操作家长控制 |
| V2 | switch-pctltcp-nro | 05-26 | 增加 TCP 服务器，PC 客户端远程管理 |
| V3 | switch-pctltcp-web | 05-27 | TCP 改为 HTTP，手机浏览器直接管理 |
| V4 | switch-pctltcp-sysmodule | 05-27 | 转为后台 sysmodule，开机自启 |
| V5 | switch-pctltcp-remote | 05-31 | 增加远程隧道，支持外出管理 |
| **V6（最终版）** | **[switch-pctltcp-remoteandlocal](https://github.com/gmaitxqqq/switch-pctltcp-remoteandlocal)** | **06-09** | **双通道：远程 + 本地局域网** |

> 📖 完整演进故事和所有版本对比，请查看最终版仓库：
> https://github.com/gmaitxqqq/switch-pctltcp-remoteandlocal

## 📊 系列工具对比

本项目共有 6 个版本，逐步演进。请根据使用场景选择合适的版本：

| 版本 | 仓库 | 类型 | 适用场景 | 核心特点 |
|------|------|------|----------|----------|
| V1 | [switch-parental-timer](https://github.com/gmaitxqqq/switch-parental-timer) | 本机 NRO | 在 Switch 上直接操作，无需网络 | PIN 验证、纯前台应用 |
| V2 | [switch-pctltcp-nro](https://github.com/gmaitxqqq/switch-pctltcp-nro) | 前台 NRO + TCP | 固定 IP 局域网，PC 客户端远程管理 | TCP 文本协议、PC Tkinter 客户端 |
| V3 | [switch-pctltcp-web](https://github.com/gmaitxqqq/switch-pctltcp-web) | 前台 NRO + Web UI | 外出时手机浏览器管理（无固定 IP） | HTTP 服务器、手机友好 UI |
| V4 | [switch-pctltcp-sysmodule](https://github.com/gmaitxqqq/switch-pctltcp-sysmodule) | 后台 sysmodule | 固定 IP 家庭环境，开机自动运行 | 后台服务、LAN only |
| V5 | [switch-pctltcp-remote](https://github.com/gmaitxqqq/switch-pctltcp-remote) | 后台 sysmodule | 需要远程控制（外出管理） | 远程隧道、长轮询 |
| **V6（推荐）** | **[switch-pctltcp-remoteandlocal](https://github.com/gmaitxqqq/switch-pctltcp-remoteandlocal)** | **后台 sysmodule** | **最完善方案，双通道控制** | **远程 + 本地、高可靠** |

> ⭐ **推荐直接使用 V6 最终版**，功能最完整。


Nintendo Switch 本机家长控制管理工具。纯 `.nro` 前台应用，直接在 Switch 上操作，无需 PC 或手机。

**版本**：v11.5 | **固件**：兼容 Atmosphere 22.1.0+

---

## 适用场景

> 在 Switch 本机上直接设置和修改家长控制，不需要任何网络或外部设备。

---

## 安装

1. 从 [Releases](../../releases) 下载 `parental_control_manager.nro`
2. 复制到 SD 卡：`/switch/parental_control_manager.nro`
3. 从 Homebrew Menu 启动（相册进入，或按住 R 键打开任意游戏）

### 运行要求

- Nintendo Switch + **Atmosphere 自制固件**
- 固件 22.1.0（其他版本可能可用，未测试）

---

## 功能

### PIN 验证

打开 app 后**必须输入 PIN** 才能进入主菜单，防止小孩随意修改。

- 系统已设置 PIN → 输入系统 PIN
- 系统未设置 PIN → 默认密码 `8473`

| 按键 | 功能 |
|------|------|
| 上 / 下 | 切换数字（0-9） |
| 左 / 右 | 移动光标 |
| A | 确认提交 |
| B | 退出 app |

最多 5 次尝试，输错自动退出。

### 主菜单

| # | 功能 | 说明 |
|---|------|------|
| 1 | View Current Status | 查看安全等级、PIN、限制开关、计时器、每日限额 |
| 2 | Set / Change PIN | 启动系统 PIN 设置界面 |
| 3 | Unlock Temporarily | 临时解除家长控制限制 |
| 4 | Set Weekly Play Time | 按天设置一周游玩时间 |
| 5 | Set Uniform Daily Time | 统一设置所有天的游玩时间 |
| 6 | Clear Play Time Limits | 清除所有每日时间限制 |
| 7 | Delete Parental Controls | 删除所有家长控制（含 PIN），不可恢复 |
| 8 | Delete Phone Pairing | 解除手机家长控制 App 配对 |

### 时间设置操作

**每周设置（按天）**：

| 按键 | 功能 |
|------|------|
| 上 / 下 | 切换天数 / 编辑时 ±1 分钟 |
| 左 / 右 | 编辑时 ±10 分钟 |
| A | 选中天数 / 确认数值 |
| B | 取消 / 返回 |
| X | 所有天快速设为 15 分钟 |
| L | 设为 0（当天禁止） |
| R | 设为无限制 |

**统一每日设置**：

| 按键 | 功能 |
|------|------|
| 上 / 下 | ±1 分钟 |
| 左 / 右 | ±10 分钟 |
| L | 设为 0（禁止） |
| R | 设为无限制 |
| A | 应用 |
| B | 取消 |

时间值：`0` = 禁止 | `1-65534` = 分钟 | `65535` = 无限制

---

## 常用操作

**每天 30 分钟**：主菜单 → 5. Set Uniform Daily Time → 调到 30 → 按 A

**临时解锁**：主菜单 → 3. Unlock Temporarily → 自动完成

**按天不同**：主菜单 → 4. Set Weekly Play Time → 选中天按 A → 调整 → 按 A 确认 → 全部设好选 Apply

---

## 已知限制

| 问题 | 说明 |
|------|------|
| 仅英文界面 | Switch 控制台只有英文字体，中文乱码 |
| 纯文本 UI | 无图形界面 |
| 需要自制固件 | `SetPlayTimerSettingsForDebug` 命令需要 CFW |

---

## 从源码编译

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=/opt/devkitpro/devkitA64
export PATH=$DEVKITA64/bin:$DEVKITPRO/tools/bin:$PATH

git clone https://github.com/gmaitxqqq/switch-parental-timer.git
cd switch-parental-timer
make clean && make
```

推送至 GitHub 后 Actions 自动构建。

---

## 同系列工具

| 项目 | 类型 | 适用场景 |
|------|------|---------|
| **switch-parental-timer**（本仓库） | 本机 NRO | 在 Switch 上直接操作，无需网络 |
| [switch-pctltcp-nro](https://github.com/gmaitxqqq/switch-pctltcp-nro) | 前台 NRO + TCP | 固定 IP 局域网，PC 客户端远程管理 |
| [switch-pctltcp-web](https://github.com/gmaitxqqq/switch-pctltcp-web) | 前台 NRO + Web UI | 外出时手机浏览器管理（无固定 IP） |
| [switch-pctltcp-sysmodule](https://github.com/gmaitxqqq/switch-pctltcp-sysmodule) | 后台 sysmodule | 固定 IP 家庭环境，开机自动运行 |

---

## 版本历史

| 版本 | 变更 |
|------|------|
| **v11.5** | PIN 验证门控，打开 app 必须输入 PIN |
| **v11.4** | UI 回退英文（CJK 字体乱码） |
| **v11.3** | 中文界面、15 分钟默认 |
| **v11.2** | 修复黑屏（consoleUpdate） |
| **v11.0** | 完全重写：放弃 sysmodule，纯 NRO + pctl IPC |
