import SwiftUI

/// 大面积使用率面积图，用于性能页主区域。
struct UsageChart: View {
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
            var line = Path()
            var firstX: CGFloat = 0
            var lastX: CGFloat = 0
            for (i, v) in data.enumerated() {
                let x = Double(window - data.count + i) / Double(window - 1) * Double(size.width)
                let y = Double(size.height) - (v / maxV) * Double(size.height)
                let pt = CGPoint(x: x, y: y)
                if i == 0 {
                    line.move(to: pt)
                    firstX = pt.x
                } else {
                    line.addLine(to: pt)
                }
                lastX = pt.x
            }

            // 面积填充：只覆盖有数据的 x 区间，避免未填满时出现斜到角落的闭合线。
            var fill = line
            fill.addLine(to: CGPoint(x: lastX, y: size.height))
            fill.addLine(to: CGPoint(x: firstX, y: size.height))
            fill.closeSubpath()
            context.fill(
                fill,
                with: .linearGradient(
                    Gradient(colors: [color.opacity(0.35), color.opacity(0.05)]),
                    startPoint: .zero,
                    endPoint: CGPoint(x: 0, y: size.height)
                )
            )
            context.stroke(line, with: .color(color), lineWidth: 2)
        }
    }
}
