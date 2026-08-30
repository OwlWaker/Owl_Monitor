import IOKit
import Foundation

/// 读取 GPU 总体使用率：从 `AGXAccelerator` 服务的 `PerformanceStatistics`
/// 字典中的 `"Device Utilization %"` 键获取（Apple 集成 GPU）。
/// 无法获取时返回 nil。
final class GPUUsageMonitor {
    /// 返回 GPU 使用率（0...100%），无法获取返回 nil。
    func utilization() -> Double? {
        let service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AGXAccelerator"))
        guard service != 0 else { return nil }
        defer { IOObjectRelease(service) }

        guard let prop = IORegistryEntryCreateCFProperty(service, "PerformanceStatistics" as CFString, kCFAllocatorDefault, 0)?.takeRetainedValue(),
              let stats = prop as? [String: Any],
              let val = stats["Device Utilization %"] as? NSNumber else { return nil }
        return Double(val.intValue)
    }
}
