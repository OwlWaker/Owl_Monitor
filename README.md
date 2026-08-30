# OwlMonitor

一款使用 **SwiftUI** 构建的原生 macOS 任务管理器应用，整体风格仿 Windows 11 任务管理器，采用标准 SwiftUI / AppKit 组件，拥有流畅、自然的系统集成与**液态玻璃（liquid-glass）**质感。

> 目标系统 macOS 26+，使用 Swift 6.2 与 Swift Package Manager 构建。

## 项目动机

macOS 官方「活动监视器」把所有进程混在一起，资源占用只显示数字。OwlMonitor 借鉴 Windows 11 任务管理器，把进程按「应用 / 后台进程」分组，并以更直观的方式呈现进程的 CPU / 内存占用。

> 本项目为产品方向的 SwiftUI 原型，替代早期的 C++ / Vulkan 版本。

## 特性

### 顶部导航
- 工具栏中的**分段控件（Segmented Control）**切换「性能 / 进程」两个页面
- 标准 AppKit / SwiftUI 材质，呈现原生半透明工具栏

### 性能页
- **统计卡片**：CPU / 内存 / 进程数
- **玻璃卡片**区域（预留图表位）

### 进程页
- 从 `NSWorkspace` 读取正在运行的应用并列出
- **搜索**按进程名过滤
- **结束进程**按钮（液态玻璃 `.glass` 风格）：对选中进程发送 `SIGTERM`
- 每行显示进程**图标**

## 技术栈

- **Swift 6.2** / **SwiftUI** / **AppKit**
- Swift Package Manager（`Package.swift`，macOS 26）

## 构建与运行

```sh
cd OwlMonitorApp
swift run
```

> 以 `swift run`（非 `.app` bundle）运行时，应用会将自身激活为普通应用，使窗口正常显示在最前。

## 目录结构

```
OwlMonitorApp/
├── Package.swift
└── Sources/
    └── OwlMonitorApp/
        ├── OwlMonitorApp.swift   # @main 入口，设置激活策略
        └── ContentView.swift     # 工具栏导航 + 性能 / 进程两个页面
```
