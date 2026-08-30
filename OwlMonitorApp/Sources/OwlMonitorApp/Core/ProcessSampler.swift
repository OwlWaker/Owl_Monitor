import AppKit
import Foundation

/// 采样器：用 libproc 枚举全部进程，读取 CPU 累计时间与常驻内存，并计算 CPU 占用率。
final class ProcessSampler {
    struct Entry { let cpu: UInt64; let time: Double }
    private var last: [pid_t: Entry] = [:]
    private var lastTime = 0.0

    init() {
        // 一次性的 ABI 布局校验：若 ProcTaskInfo 与 C 的 proc_taskinfo 不一致，
        // 提前崩溃而不是静默读到错位数据。
        precondition(MemoryLayout<ProcTaskInfo>.size == ProcTaskInfo.expectedSize,
                     "ProcTaskInfo 布局与 C 的 struct proc_taskinfo 不一致（期望 \(ProcTaskInfo.expectedSize) 字节）")
    }

    func sample() -> [ProcessItem] {
        let now = ProcessInfo.processInfo.systemUptime
        let dt = now - lastTime
        let appPids = Set(NSWorkspace.shared.runningApplications
            .filter { $0.activationPolicy == .regular }
            .map { $0.processIdentifier })
        var items: [ProcessItem] = []
        for pid in listPids() {
            guard let info = taskInfo(pid) else { continue }
            let isApp = appPids.contains(pid)
            var cpuPercent = 0.0
            if let prev = last[pid], dt > 0.01 {
                let delta = info.cpu > prev.cpu ? info.cpu - prev.cpu : 0
                cpuPercent = Double(delta) / 1e9 / dt * 100.0
            }
            last[pid] = Entry(cpu: info.cpu, time: now)
            let running = NSRunningApplication(processIdentifier: pid)
            let name = running?.localizedName ?? info.name
            let icon = running?.icon
            items.append(ProcessItem(id: pid,
                                     name: name.isEmpty ? "N/A" : name,
                                     icon: icon,
                                     isApp: isApp,
                                     cpuPercent: cpuPercent,
                                     memBytes: info.mem))
        }
        lastTime = now
        // 清理已退出进程的缓存
        let live = Set(items.map { $0.id })
        last = last.filter { live.contains($0.key) }
        return items
    }

    private func listPids() -> [pid_t] {
        let size = proc_listpids(UInt32(PROC_ALL_PIDS), 0, nil, 0)
        guard size > 0 else { return [] }
        let count = Int(size) / MemoryLayout<pid_t>.size
        var pids = [pid_t](repeating: 0, count: count)
        _ = proc_listpids(UInt32(PROC_ALL_PIDS), 0, &pids, size)
        return pids.filter { $0 > 0 }
    }

    private func taskInfo(_ pid: pid_t) -> (cpu: UInt64, mem: UInt64, name: String)? {
        var info = ProcTaskInfo()
        let sz = Int32(MemoryLayout<ProcTaskInfo>.size)
        let r = proc_pidinfo(pid, Int32(PROC_PIDTASKINFO), 0, &info, sz)
        guard r == sz else { return nil }
        var buf = [CChar](repeating: 0, count: 256)
        let n = proc_name(pid, &buf, UInt32(buf.count))
        let bytes = buf.prefix(while: { $0 != 0 }).map { UInt8(bitPattern: $0) }
        let name = n > 0 ? String(decoding: bytes, as: UTF8.self) : ""
        return (info.pti_total_user + info.pti_total_system, info.pti_resident_size, name)
    }
}
