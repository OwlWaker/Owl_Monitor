import AppKit
import SwiftUI

/// 获取所在窗口并把导航栏毛玻璃以「不拦截事件」的方式铺在标题栏后面，
/// 让标题栏（含标题文字）仍可拖动窗口。
struct WindowAccessor: NSViewRepresentable {
    func makeNSView(context: Context) -> NSView {
        let view = NSView()
        DispatchQueue.main.async { attachIfNeeded(view) }
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {
        attachIfNeeded(nsView)
    }

    private func attachIfNeeded(_ view: NSView) {
        guard let window = view.window,
              let themeFrame = window.contentView?.superview,
              !themeFrame.subviews.contains(where: { $0 is PassthroughVisualEffectView }) else { return }

        let effect = PassthroughVisualEffectView()
        effect.material = .titlebar
        effect.blendingMode = .behindWindow
        effect.state = .active
        effect.translatesAutoresizingMaskIntoConstraints = false
        themeFrame.addSubview(effect, positioned: .below, relativeTo: nil)

        NSLayoutConstraint.activate([
            effect.leadingAnchor.constraint(equalTo: themeFrame.leadingAnchor),
            effect.trailingAnchor.constraint(equalTo: themeFrame.trailingAnchor),
            effect.topAnchor.constraint(equalTo: themeFrame.topAnchor),
            effect.heightAnchor.constraint(equalToConstant: 52)
        ])
    }
}
