import AppKit

/// 一个进程条目（含分组、CPU、内存）。
struct ProcessItem: Identifiable {
    let id: pid_t
    let name: String
    let icon: NSImage?
    let isApp: Bool          // 应用 vs 后台进程
    let cpuPercent: Double   // 占用率（%）
    let memBytes: UInt64     // 常驻内存（字节）
}
