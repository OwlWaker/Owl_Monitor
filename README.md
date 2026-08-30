# OwlMonitor

A native macOS task manager built with **SwiftUI**, inspired by Windows 11 Task Manager, with a **liquid-glass** look and system-native components.

> Target: macOS 26+, Swift 6.2, Swift Package Manager.

---

## English

OwlMonitor is a native macOS task manager that presents process CPU/memory usage more intuitively than the official Activity Monitor. It borrows the layout of Windows 11 Task Manager, grouping processes into **Apps** and **Background processes**, and includes a Windows-style **Performance** page with real-time charts.

> This project is the result of a **major rewrite**: it was originally a C++/Vulkan application and has been completely rewritten in SwiftUI. The legacy C++/CMake codebase was removed in **v1.0.0**.

### Features

**Top navigation**

- A segmented control in the toolbar switches between **Performance / Processes / Settings**.
- Native AppKit/SwiftUI translucent (liquid-glass) toolbar.

**Performance page**

- Left category sidebar: CPU, Memory, Disk, Network, GPU — each with a mini sparkline and an accent color.
- Right main area: a large area chart (fixed rolling time window) plus stat cards per category.
- CPU / Memory / Disk / GPU use real data; Network uses mock data for now.

**Processes page**

- Lists processes grouped into **Apps** / **Background processes**.
- Search filters by name; each row shows the icon, PID (no thousands separator), CPU %, and memory.
- **End Process** opens a confirmation dialog; if the process doesn't exit in time, it offers a **Force Quit** (SIGKILL) prompt.

**Settings page**

- Adjustable performance/process refresh interval, chart history window, and end-process wait time (persisted via `UserDefaults`).

### Tech stack

- **Swift 6.2** / **SwiftUI** / **AppKit**
- Swift Package Manager (`OwlMonitorApp/Package.swift`, macOS 26)

### Build & run

```sh
cd OwlMonitorApp
swift run
```

When run via `swift run` (not an `.app` bundle), the app activates itself as a regular application so the window appears in the foreground.

### Release / DMG

Pushing a `v*` tag triggers `.github/workflows/release.yml`, which builds the app, packages `dist/OwlMonitor.dmg` via `scripts/package_dmg.sh`, and uploads it to the matching GitHub Release.

---

## 中文

OwlMonitor 是一款用 **SwiftUI** 构建的原生 macOS 任务管理器，风格仿 Windows 11 任务管理器，采用系统原生组件与**液态玻璃（liquid-glass）**质感。

> 目标系统 macOS 26+，Swift 6.2，Swift Package Manager。

### 项目动机

macOS 官方「活动监视器」把所有进程混在一起、资源占用只显示数字；OwlMonitor 借鉴 Windows 11 任务管理器，把进程按「应用 / 后台进程」分组，并更直观地展示 CPU / 内存占用。

> 本项目为**一次大重构的产物**：早期为 C++/Vulkan 版本，现已全面重写为 SwiftUI；旧 C++/CMake 代码库已在 **v1.0.0** 中移除。

### 特性

**顶部导航**

- 工具栏**分段控件**切换「性能 / 进程 / 设置」三个页面。
- 原生 AppKit/SwiftUI 半透明（液态玻璃）工具栏。

**性能页**

- 左侧类别侧栏：CPU / 内存 / 磁盘 / 网络 / GPU，各带迷你走势图与主题色。
- 右侧主区：大面积走势图（固定滚动时间窗）+ 各类别统计卡。
- CPU / 内存 / 磁盘 / GPU 为真实数据；网络暂用模拟数据。

**进程页**

- 进程按「应用 / 后台进程」分组展示。
- 支持按名称搜索；每行显示图标、PID（无千位分隔）、CPU%、内存。
- **结束进程**带确认弹窗；若进程未在限定时间内退出，会弹出**强制结束（SIGKILL）**确认。

**设置页**

- 可调性能/进程刷新间隔、图表历史窗口、结束进程检测等待时间（经 `UserDefaults` 持久化）。

### 技术栈

- **Swift 6.2** / **SwiftUI** / **AppKit**
- Swift Package Manager（`OwlMonitorApp/Package.swift`，macOS 26）

### 构建与运行

```sh
cd OwlMonitorApp
swift run
```

以 `swift run`（非 `.app` bundle）运行时，应用会自行激活为普通应用，使窗口正常显示在最前。

### 发布 / DMG

推送 `v*` 标签会触发 `.github/workflows/release.yml`：构建应用 → 用 `scripts/package_dmg.sh` 打包 `dist/OwlMonitor.dmg` → 上传到对应 GitHub Release。
