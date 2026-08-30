import SwiftUI

/// 玻璃卡片容器。
struct GlassCard<Content: View>: View {
    @ViewBuilder var content: Content
    var body: some View {
        content
            .padding(AppMetrics.cardPadding)
            .background(RoundedRectangle(cornerRadius: AppMetrics.cardCornerRadius).fill(.ultraThinMaterial))
            .overlay(RoundedRectangle(cornerRadius: AppMetrics.cardCornerRadius)
                .stroke(.white.opacity(AppMetrics.strokeOpacity), lineWidth: 1))
    }
}
