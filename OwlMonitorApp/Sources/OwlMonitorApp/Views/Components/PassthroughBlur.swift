import AppKit
import SwiftUI

/// 不拦截鼠标事件的毛玻璃视图（`hitTest` 返回 nil），用于标题栏等需要可拖动的区域。
final class PassthroughVisualEffectView: NSVisualEffectView {
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}

/// 供 SwiftUI 使用的 passthrough 毛玻璃（不拦截拖动事件）。
struct PassthroughBlur: NSViewRepresentable {
    var material: NSVisualEffectView.Material = .titlebar

    func makeNSView(context: Context) -> NSVisualEffectView {
        let view = PassthroughVisualEffectView()
        view.material = material
        view.blendingMode = .behindWindow
        view.state = .active
        return view
    }

    func updateNSView(_ nsView: NSVisualEffectView, context: Context) {
        nsView.material = material
    }
}
