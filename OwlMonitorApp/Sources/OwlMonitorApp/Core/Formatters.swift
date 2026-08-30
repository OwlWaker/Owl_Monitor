import Foundation

/// 字节格式化：B / KB / MB / GB / TB。
func fmtBytes(_ bytes: UInt64) -> String {
    let b = Double(bytes)
    let units = ["B", "KB", "MB", "GB", "TB"]
    var v = b; var i = 0
    while v >= 1024 && i < units.count - 1 { v /= 1024; i += 1 }
    return i == 0 ? String(format: "%.0f %@", v, units[i])
                  : String(format: "%.1f %@", v, units[i])
}
