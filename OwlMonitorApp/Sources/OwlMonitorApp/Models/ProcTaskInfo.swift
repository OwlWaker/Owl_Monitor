import Foundation

/// 与 C 的 `struct proc_taskinfo` 布局保持一致，用于 `proc_pidinfo` 读取 CPU / 内存。
///
/// 字段来源：macOS SDK 的 `/usr/include/libproc.h` 中 `proc_taskinfo` 的字段顺序与宽度。
/// 这是对 C 结构的镜像，绝不能随意增删字段或改变宽度，否则 `proc_pidinfo` 会读到错位的数据。
struct ProcTaskInfo {
    var pti_virtual_size: UInt64 = 0
    var pti_resident_size: UInt64 = 0
    var pti_total_user: UInt64 = 0
    var pti_total_system: UInt64 = 0
    var pti_threads_user: UInt64 = 0
    var pti_threads_system: UInt64 = 0
    var pti_policy: Int32 = 0
    var pti_faults: Int32 = 0
    var pti_pageins: Int32 = 0
    var pti_cow_faults: Int32 = 0
    var pti_messages_sent: Int32 = 0
    var pti_messages_received: Int32 = 0
    var pti_syscalls_mach: Int32 = 0
    var pti_syscalls_unix: Int32 = 0
    var pti_csw: Int32 = 0
    var pti_threadnum: Int32 = 0
    var pti_numrunning: Int32 = 0
    var pti_priority: Int32 = 0

    /// C 结构 `struct proc_taskinfo` 的字节数：6×UInt64 + 12×Int32 = 96。
    static let expectedSize = 96
}
