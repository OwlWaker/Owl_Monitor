import SwiftUI

/// 性能页左侧的类别。
enum PerformanceCategory: String, CaseIterable, Identifiable {
    case cpu = "CPU"
    case memory = "内存"
    case disk = "磁盘"
    case network = "网络"
    case gpu = "GPU"

    var id: String { rawValue }

    /// SF Symbol 图标名。
    var symbol: String {
        switch self {
        case .cpu: return "cpu"
        case .memory: return "memorychip"
        case .disk: return "internaldrive"
        case .network: return "network"
        case .gpu: return "display"
        }
    }

    /// 主题色（用于走势图与图标）。
    var accent: Color {
        switch self {
        case .cpu: return .blue
        case .memory: return .purple
        case .disk: return .teal
        case .network: return .green
        case .gpu: return .orange
        }
    }
}
