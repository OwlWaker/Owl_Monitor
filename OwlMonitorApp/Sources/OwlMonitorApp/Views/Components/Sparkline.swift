import SwiftUI

/// 小型走势图，用于侧栏每个类别的 sparkline。
struct Sparkline: View {
    let data: [Double]
    let color: Color
    /// 固定时间窗的采样点数，最新数据贴右、旧数据向左滚动。
    var windowSize: Int = 60
    /// 固定的 y 最大值；为 nil 时按数据最大值自动归一化（用于吞吐量等非百分比数据）。
    var maxValue: Double? = nil

    var body: some View {
        Canvas { context, size in
            guard data.count > 1 else { return }
            let maxV = maxValue ?? max(data.max() ?? 0, 1e-6)
            // 固定时间窗（windowSize 个采样点），最新数据贴右，旧数据向左滚动。
            let window = max(data.count, windowSize)
            let path = Path { p in
                for (i, v) in data.enumerated() {
                    let x = Double(window - data.count + i) / Double(window - 1) * Double(size.width)
                    let y = Double(size.height) - (v / maxV) * Double(size.height)
                    let pt = CGPoint(x: x, y: y)
                    if i == 0 { p.move(to: pt) } else { p.addLine(to: pt) }
                }
            }
            context.stroke(path, with: .color(color), lineWidth: 1)
        }
    }
}
