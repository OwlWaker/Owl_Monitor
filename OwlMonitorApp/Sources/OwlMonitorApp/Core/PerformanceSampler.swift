import Darwin
import Foundation
import IOKit

/// 采样 CPU / 内存 / 磁盘 / GPU 的真实数据与网络的模拟数据，并维护历史曲线。
final class PerformanceSampler {
    private var lastCpuTicks: Int64 = 0
    private var lastCpuIdle: Int64 = 0
    private var lastCpuTime: Double = 0
    private var tick = 0

    /// 每个类别的历史曲线（最多保留 historyWindow 个采样点）：CPU/内存/GPU 为 0...100%，磁盘为 MB/s。
    private(set) var histories: [PerformanceCategory: [Double]] = [:]
    var historyWindow: Int = 60
    private(set) var memUsed: UInt64 = 0
    private(set) var diskReadMBps: Double = 0
    private(set) var diskWriteMBps: Double = 0
    private(set) var diskReadHistory: [Double] = []
    private(set) var diskWriteHistory: [Double] = []
    let memTotal: UInt64 = ProcessInfo.processInfo.physicalMemory

    private let gpu = GPUUsageMonitor()
    private var lastDiskRead: UInt64 = 0
    private var lastDiskWrite: UInt64 = 0
    private var lastDiskTime: Double = 0

    init() {
        for cat in PerformanceCategory.allCases { histories[cat] = [] }
    }

    /// 采样一次，并推动各类别历史曲线。
    func sample() {
        tick += 1
        let now = ProcessInfo.processInfo.systemUptime

        // CPU（真实）
        if let t = cpuTicks() {
            if lastCpuTicks > 0 {
                let dt = now - lastCpuTime
                if dt > 0.01 {
                    let totalDelta = Double(t.total - lastCpuTicks)
                    let idleDelta = Double(t.idle - lastCpuIdle)
                    let usage = totalDelta > 0 ? (1.0 - idleDelta / totalDelta) * 100.0 : 0
                    push(usage, for: .cpu)
                }
            }
            lastCpuTicks = t.total
            lastCpuIdle = t.idle
            lastCpuTime = now
        }

        // 内存（真实）
        if let m = memoryStats() {
            memUsed = m.used
            push(Double(m.used) / Double(m.total) * 100.0, for: .memory)
        }

        // 磁盘（真实 I/O 吞吐，MB/s）
        if let io = diskIO() {
            let dt = now - lastDiskTime
            if lastDiskRead > 0, dt > 0.01 {
                let readMB = Double(io.read > lastDiskRead ? io.read - lastDiskRead : 0) / (1024 * 1024) / dt
                let writeMB = Double(io.write > lastDiskWrite ? io.write - lastDiskWrite : 0) / (1024 * 1024) / dt
                diskReadMBps = max(0, readMB)
                diskWriteMBps = max(0, writeMB)
                appendTo(&diskReadHistory, value: readMB)
                appendTo(&diskWriteHistory, value: writeMB)
                histories[.disk] = diskReadHistory
            }
            lastDiskRead = io.read
            lastDiskWrite = io.write
            lastDiskTime = now
        }

        // GPU（真实，AGXAccelerator 使用率）
        if let gpuUsage = gpu.utilization() {
            push(gpuUsage, for: .gpu)
        }

        // 网络（模拟走势）
        push(mockWave(base: 12, amp: 26, seed: tick), for: .network)
    }

    private func push(_ value: Double, for cat: PerformanceCategory) {
        let v = min(max(value, 0), 100)
        histories[cat, default: []].append(v)
        if histories[cat]!.count > historyWindow { histories[cat]!.removeFirst() }
    }

    private func mockWave(base: Double, amp: Double, seed: Int) -> Double {
        base + amp * sin(Double(seed) * 0.4) + Double.random(in: -3...3)
    }

    private func appendTo(_ array: inout [Double], value: Double) {
        array.append(value)
        if array.count > historyWindow { array.removeFirst() }
    }

    /// 读取所有块存储设备的累计读写字节数。
    private func diskIO() -> (read: UInt64, write: UInt64)? {
        let matching = IOServiceMatching("IOBlockStorageDriver")
        var iterator = io_iterator_t()
        guard IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) == KERN_SUCCESS else { return nil }
        defer { IOObjectRelease(iterator) }

        var read: UInt64 = 0
        var write: UInt64 = 0
        var found = false
        var service = IOIteratorNext(iterator)
        while service != 0 {
            defer { IOObjectRelease(service) }
            var properties: Unmanaged<CFMutableDictionary>?
            if IORegistryEntryCreateCFProperties(service, &properties, kCFAllocatorDefault, 0) == KERN_SUCCESS,
               let cfProps = properties?.takeRetainedValue(),
               let stats = (cfProps as NSDictionary)["Statistics"] as? [String: Any] {
                if let r = (stats["Bytes (Read)"] as? NSNumber)?.uint64Value { read += r; found = true }
                if let w = (stats["Bytes (Write)"] as? NSNumber)?.uint64Value { write += w; found = true }
            }
            service = IOIteratorNext(iterator)
        }
        return found ? (read, write) : nil
    }

    /// 读取所有 CPU 核心的累计 tick（用户/系统/空闲/调度）。
    private func cpuTicks() -> (total: Int64, idle: Int64)? {
        var info = host_cpu_load_info()
        var count = mach_msg_type_number_t(MemoryLayout<host_cpu_load_info>.stride / MemoryLayout<Int32>.stride)
        let kr = withUnsafeMutablePointer(to: &info) { ptr -> kern_return_t in
            ptr.withMemoryRebound(to: Int32.self, capacity: Int(count)) { p in
                host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, p, &count)
            }
        }
        guard kr == KERN_SUCCESS else { return nil }
        let user = Int64(info.cpu_ticks.0)
        let system = Int64(info.cpu_ticks.1)
        let idle = Int64(info.cpu_ticks.2)
        let nice = Int64(info.cpu_ticks.3)
        return (user + system + idle + nice, idle)
    }

    /// 读取物理内存占用（活跃 + 非活跃 + 已被系统保留）。
    private func memoryStats() -> (used: UInt64, total: UInt64)? {
        var stat = vm_statistics64()
        var count = mach_msg_type_number_t(MemoryLayout<vm_statistics64>.stride / MemoryLayout<Int32>.stride)
        let kr = withUnsafeMutablePointer(to: &stat) { ptr -> kern_return_t in
            ptr.withMemoryRebound(to: Int32.self, capacity: Int(count)) { p in
                host_statistics64(mach_host_self(), HOST_VM_INFO64, p, &count)
            }
        }
        guard kr == KERN_SUCCESS else { return nil }
        let pageSize = UInt64(getpagesize())
        let used = (UInt64(stat.active_count) + UInt64(stat.inactive_count) + UInt64(stat.wire_count)) * pageSize
        return (used, memTotal)
    }
}
