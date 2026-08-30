import SwiftUI

/// 统计卡：RoundedRectangle 背景 + 液态玻璃材质（.ultraThinMaterial）。
struct StatCard: View {
    let title: String
    let value: String
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title).font(.caption).foregroundStyle(.secondary)
            Text(value).font(.title2.bold())
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(AppMetrics.cardPadding)
        .background(RoundedRectangle(cornerRadius: AppMetrics.cardCornerRadius).fill(.ultraThinMaterial))
        .overlay(RoundedRectangle(cornerRadius: AppMetrics.cardCornerRadius)
            .stroke(.white.opacity(AppMetrics.strokeOpacity), lineWidth: 1))
        .glassEffect()
    }
}
