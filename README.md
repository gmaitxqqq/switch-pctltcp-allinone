# PCTL All-in-One

Nintendo Switch 家长控制管理工具，**控制台 UI + 手机浏览器 Web UI 二合一**。

- **控制台 UI**：在 Switch 本机上用手柄操作
- **Web UI**：手机/PC 浏览器访问，中文界面，更方便
- **修复**：设置时间限额后倒计时正确启动（原版 bug 已修）

**版本**：v1.0.0 | **固件**：兼容 Atmosphere 22.1.0+

---

## 功能一览

| 功能 | 控制台 UI | Web UI |
|------|-----------|---------|
| 查看当前状态 | ✅ | ✅ |
| 设置/修改 PIN | ✅ | ❌ |
| 临时解除限制 | ✅ | ✅ |
| 设置每日游玩时间 | ✅ | ✅ |
| 统一设置所有天 | ✅ | ✅ |
| 清除时间限制 | ✅ | ✅ |
| 删除家长控制 | ✅ | ❌ |
| 删除手机配对 | ✅ | ❌ |

---

## 安装

1. 从 [Releases](../../releases) 下载 `pctltcp-allinone.nro`
2. 复制到 SD 卡：`/switch/pctltcp-allinone.nro`
3. 从 Homebrew Menu 启动

### 运行要求

- Nintendo Switch + **Atmosphere 自制固件**
- 固件 22.1.0（其他版本可能可用，未测试）
- WiFi 连接（Web UI 需要）

---

## 使用方式

### 方式一：控制台 UI（手柄操作）

启动后用 Switch 手柄操作，界面全英文。

**PIN 验证**：打开 app 后必须输入 PIN 才能进入主菜单。

| 按键 | 功能 |
|------|------|
| 上 / 下 | 切换选项 |
| A | 确认 |
| B | 返回 / 退出 |
| X | 快速设置（每周 15 分钟） |

### 方式二：Web UI（手机/PC 浏览器）

启动 app 后，在手机或 PC 浏览器里打开：

```
http://<Switch 的 IP 地址>:8081
```

**如何找到 Switch 的 IP 地址：**
进入 Switch 系统设置 → 互联网 → 连接设置 → 选择当前连接的网络 → 点击「设置」→ 查看「IP 地址」

Web UI 是**中文界面**，比控制台方便很多。

---

## Web UI 功能说明

| 功能 | 说明 |
|------|------|
| 查看状态 | 显示安全等级、PIN 状态、倒计时剩余时间 |
| 加时 | 临时增加游玩时间（5/15/30/60 分钟） |
| 修改时间 | 直接设置当天剩余时间 |
| 设置每日限额 | 按天设置一周游玩时间 |
| 统一设置 | 所有天设置相同的时间限额 |
| 清除限制 | 移除所有时间限制 |
| 临时解锁 | 输入 PIN 后临时解除限制 |

---

## 已知问题

| 问题 | 说明 |
|------|------|
| 控制台 UI 只有英文 | Switch 控制台中文字体显示乱码，暂用英文 |
| Web UI 是中文 | 需要在手机/PC 上访问 |
| 需要自制固件 | `SetPlayTimerSettingsForDebug` 命令需要 CFW |

---

## 从源码编译

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=/opt/devkitpro/devkitA64
export PATH=$DEVKITA64/bin:$DEVKITPRO/tools/bin:$PATH

git clone https://github.com/gmaitxqqq/switch-pctltcp-allinone.git
cd switch-pctltcp-allinone
make clean && make
```

推送至 GitHub 后 Actions 自动构建，在 Releases 页面下载。

---

## 同系列工具

| 项目 | 类型 | 适用场景 |
|------|------|---------|
| [switch-parental-timer](https://github.com/gmaitxqqq/switch-parental-timer) | 本机 NRO | 在 Switch 上直接操作，无需网络 |
| [switch-pctltcp-nro](https://github.com/gmaitxqqq/switch-pctltcp-nro) | 前台 NRO + TCP | 固定 IP 局域网，PC 客户端远程管理 |
| [switch-pctltcp-remote](https://github.com/gmaitxqqq/switch-pctltcp-remote) | 后台 sysmodule | 需要远程控制（外出管理） |
| **switch-pctltcp-allinone**（本仓库） | 本机 NRO + Web UI | **推荐：控制台 + 浏览器双操作方式** |

---

## 版本历史

| 版本 | 变更 |
|------|------|
| **v1.0.0** | 初始版本：合并控制台 UI + Web UI，修复倒计时不启动 bug |
